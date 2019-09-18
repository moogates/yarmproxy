#include "redis_mget_command.h"

#include "base/logging.h"

#include "error_code.h"
#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

std::atomic_int redis_mget_cmd_count;

RedisMgetCommand::BackendQuery::~BackendQuery() {
}

bool RedisMgetCommand::GroupKeysByEndpoint(const redis::BulkArray& ba) {
  ip::tcp::endpoint last_endpoint;
  const char* current_bulks_data = nullptr;
  size_t current_bulks_count = 0;
  size_t current_bulks_bytes = 0;

  for(size_t i = 1; i < ba.total_bulks(); ++i) {
    const redis::Bulk& bulk = ba[i];
    ip::tcp::endpoint current_endpoint = BackendLoactor::Instance().Locate(bulk.payload_data(), bulk.payload_size(), "REDIS_bj");
    // ip::tcp::endpoint current_endpoint = BackendLoactor::Instance().Locate(bulk.payload_data(), bulk.payload_size());

    LOG_DEBUG << "GroupKeysByEndpoint key=" << bulk.to_string() << " ep=" << current_endpoint
              << " last_ep=" << last_endpoint;

    if (current_endpoint == last_endpoint) {
      ++current_bulks_count;
      current_bulks_bytes += bulk.total_size();
      LOG_DEBUG << "GroupKeysByEndpoint subquery=[" << std::string(bulk.raw_data(), bulk.total_size()) << "]";
    } else {
      if (current_bulks_data != nullptr) {
        std::string subquery(redis::BulkArray::SerializePrefix(current_bulks_count + 1));
        subquery.append("$4\r\nmget\r\n"); // (ba[0].raw_data(), ba[0].total_size())
        subquery.append(current_bulks_data, current_bulks_bytes);

        LOG_DEBUG << "GroupKeysByEndpoint create subquery ep=" << last_endpoint
              << " bulks_count=" << current_bulks_count
              << " query=(" << subquery
              << ") current_key=" << bulk.to_string() << "(not included)";

        // endpoint_keys_list->emplace_back(last_endpoint, std::move(subquery));
        query_set_.emplace_back(new BackendQuery(last_endpoint, std::move(subquery))); // TODO : zero copy

        current_bulks_data = nullptr;
      }
      last_endpoint = current_endpoint;
      current_bulks_data = bulk.raw_data();
      current_bulks_count = 1;
      current_bulks_bytes = bulk.total_size();
      LOG_DEBUG << "GroupKeysByEndpoint subquery=[" << std::string(bulk.raw_data(), bulk.total_size()) << "]";
    }
  }

  if (current_bulks_data != nullptr) {
    std::string subquery(redis::BulkArray::SerializePrefix(current_bulks_count + 1));
    subquery.append("$4\r\nmget\r\n"); // (ba[0].raw_data(), ba[0].total_size())
    subquery.append(current_bulks_data, current_bulks_bytes);
    // endpoint_keys_list->emplace_back(last_endpoint, std::move(subquery));
    query_set_.emplace_back(new BackendQuery(last_endpoint, std::move(subquery))); // TODO : zero copy

    LOG_DEBUG << "GroupKeysByEndpoint create last subquery ep=" << last_endpoint
              << " bulks_count=" << current_bulks_count;
  }
  return true;
}

RedisMgetCommand::RedisMgetCommand(std::shared_ptr<ClientConnection> client,
                  const redis::BulkArray& ba)
    : Command(client)
    , completed_backends_(0)
    , unreachable_backends_(0)
{
  GroupKeysByEndpoint(ba);
}

RedisMgetCommand::RedisMgetCommand(std::shared_ptr<ClientConnection> client,
    size_t keys_count,
    std::list<std::pair<ip::tcp::endpoint, std::string>>&& endpoint_query_list)
    : Command(client)
    , completed_backends_(0)
    , unreachable_backends_(0)
{
  for(auto& it : endpoint_query_list) {
    LOG_DEBUG << "RedisMgetCommand ctor, create query ep=" << it.first << " query=(" << it.second << ")";
    query_set_.emplace_back(new BackendQuery(it.first, std::move(it.second)));
  }
  LOG_WARN << "RedisMgetCommand ctor, query_set_.size=" << query_set_.size()
            << " count=" << ++redis_mget_cmd_count;
}

RedisMgetCommand::~RedisMgetCommand() {
  for(auto& query : query_set_) {
    if (!query->backend_conn_) {
      LOG_DEBUG << "RedisMgetCommand dtor Release null backend";
    } else {
      LOG_DEBUG << "RedisMgetCommand dtor Release backend";
      backend_pool()->Release(query->backend_conn_);
    }
  }
  LOG_DEBUG << "RedisMgetCommand dtor, cmd=" << this << " count=" << --redis_mget_cmd_count;
}

void RedisMgetCommand::WriteQuery() {
  for(auto& query : query_set_) {
    if (!query->backend_conn_) {
      query->backend_conn_ = AllocateBackend(query->backend_endpoint_);
      {
        // redis mget special
        waiting_reply_queue_.push_back(query->backend_conn_);
        if (!replying_backend_) {
          replying_backend_ = query->backend_conn_;
        }
      }
    }
    LOG_DEBUG << "RedisMgetCommand WriteQuery cmd=" << this
              << " backend=" << query->backend_conn_ << ", query=("
              << query->query_line_.substr(0, query->query_line_.size() - 2) << ")";
    query->backend_conn_->WriteQuery(query->query_line_.data(), query->query_line_.size());
  }
}

void RedisMgetCommand::TryMarkLastBackend(std::shared_ptr<BackendConn> backend) {
  if (received_reply_backends_.insert(backend).second) {
    if (received_reply_backends_.size() == query_set_.size()) {
      last_backend_ = backend;
    }
  }
}

// return : is the backend successfully activated
bool RedisMgetCommand::TryActivateReplyingBackend(std::shared_ptr<BackendConn> backend) {
  // ok, same as memcache gets
  if (replying_backend_ == nullptr) {
    replying_backend_ = backend;
    LOG_DEBUG << "TryActivateReplyingBackend ok, backend=" << backend << " replying_backend_=" << replying_backend_;
    return true;
  }
  return backend == replying_backend_;
}

void RedisMgetCommand::BackendReadyToReply(std::shared_ptr<BackendConn> backend) {
  if (client_conn_->IsFirstCommand(shared_from_this())
      && TryActivateReplyingBackend(backend)) {
    if (backend->finished()) { // 新收的新数据，可能不需要转发，例如收到的刚好是"END\r\n"
      RotateReplyingBackend(backend->recyclable());
    } else {
      TryWriteReply(backend);
    }
  } else {
    // push backend into waiting_reply_queue_ if not existing
    // memcached gets specail
  //if (std::find(waiting_reply_queue_.begin(), waiting_reply_queue_.end(),
  //              backend) == waiting_reply_queue_.end()) {
  //  waiting_reply_queue_.push_back(backend);
  //}
  }
}

void RedisMgetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  TryMarkLastBackend(backend);
  if (ec != ErrorCode::E_SUCCESS
      || ParseReply(backend) == false) {
    LOG_WARN << "RedisMgetCommand::OnBackendReplyReceived error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  LOG_DEBUG << "RedisMgetCommand::OnBackendReplyReceived, endpoint="
           << backend->remote_endpoint() << " backend=" << backend
           << " finished=" << backend->finished();
  BackendReadyToReply(backend);
  backend->TryReadMoreReply(); // backend 正在read more的时候，不能memmove，不然写回的数据位置会相对漂移
}


void RedisMgetCommand::OnBackendConnectError(std::shared_ptr<BackendConn> backend) {
  LOG_DEBUG << "RedisMgetCommand::OnBackendConnectError endpoint="
            << backend->remote_endpoint() << " backend=" << backend;
  ++unreachable_backends_;
  TryMarkLastBackend(backend);

  if (true || backend == last_backend_) {
    static const char END_RN[] = "*-1\r\n"; // TODO : 统一放置错误码
    backend->SetReplyData(END_RN, sizeof(END_RN) - 1);
    LOG_WARN << "RedisMgetCommand::OnBackendConnectError, endpoint="
            << backend->remote_endpoint() << " backend=" << backend;
  } else {
    LOG_WARN << "RedisMgetCommand::OnBackendConnectError not last, endpoint="
            << backend->remote_endpoint() << " backend=" << backend;
  }
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  BackendReadyToReply(backend);
}

void RedisMgetCommand::StartWriteReply() {
  NextBackendStartReply();
}

void RedisMgetCommand::NextBackendStartReply() {
  LOG_DEBUG << "RedisMgetCommand::OnWriteReplyEnabled cmd=" << this
            << " last replying_backend_=" << replying_backend_;
  if (waiting_reply_queue_.size() > 0) {
    auto next_backend = waiting_reply_queue_.front();
    waiting_reply_queue_.pop_front();

    LOG_DEBUG << "RedisMgetCommand::OnWriteReplyEnabled activate ready backend,"
              << " backend=" << next_backend;
    if (next_backend->finished()) {
      LOG_DEBUG << "OnWriteReplyEnabled backend=" << next_backend << " empty reply";
      RotateReplyingBackend(next_backend->recyclable());
    } else {
      TryWriteReply(next_backend);
      replying_backend_ = next_backend;
    }
  } else {
    LOG_DEBUG << "RedisMgetCommand::OnWriteReplyEnabled no ready backend to activate,"
              << " last_replying_backend_=" << replying_backend_;
    replying_backend_ = nullptr;
  }
}

bool RedisMgetCommand::HasUnfinishedBanckends() const {
  LOG_DEBUG << "RedisMgetCommand::HasUnfinishedBanckends"
            << " unreachable_backends_=" << unreachable_backends_
            << " completed_backends_=" << completed_backends_ 
            << " total_backends=" << query_set_.size();
  return unreachable_backends_ + completed_backends_ < query_set_.size();
}

void RedisMgetCommand::RotateReplyingBackend(bool recyclable) {
  if (recyclable) {
    ++completed_backends_;
    LOG_DEBUG << "RotateReplyingBackend ++completed_backends_";
  }
  if (HasUnfinishedBanckends()) {
    LOG_DEBUG << "RedisMgetCommand::Rotate to next backend";
    NextBackendStartReply();
  } else {
    LOG_DEBUG << "RedisMgetCommand::Rotate to next COMMAND";
    client_conn_->RotateReplyingCommand();
  }
}

bool RedisMgetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  if (backend->buffer()->unparsed_bytes() == 0) {
    if (backend->buffer()->parsed_unreceived_bytes() == 0
        && absent_bulks_tracker_.find(backend) == absent_bulks_tracker_.end()) {
      LOG_WARN << "RedisMgetCommand::ParseReply last item all data received";
      backend->set_reply_recv_complete();
    }
    return true;
  }

  LOG_WARN << "RedisMgetCommand::OnBackendReplyReceived ParseReply, backend=" << backend;

  while(backend->buffer()->unparsed_bytes() > 0) {
    const char * entry = backend->buffer()->unparsed_data();
    size_t unparsed_bytes = backend->buffer()->unparsed_bytes();

    auto absent_it = absent_bulks_tracker_.find(backend);
    if (absent_it == absent_bulks_tracker_.end()) {
      LOG_DEBUG << "RedisMgetCommand::OnBackendReplyReceived unparsed_bytes=" << unparsed_bytes
               << " data=[" << std::string(entry, unparsed_bytes)
               << "] backend=" << backend;
      redis::BulkArray bulk_array(entry, unparsed_bytes);
      if (bulk_array.parsed_size() < 0) {
        LOG_WARN << "RedisMgetCommand::OnBackendReplyReceived ParseReply error, backend=" << backend
                 << " data=" << std::string(entry, unparsed_bytes);
        return false;
      }
      if (bulk_array.parsed_size() == 0) {
        LOG_WARN << "RedisMgetCommand::OnBackendReplyReceived ParseReply need more data, backend=" << backend
                 << " data=" << std::string(entry, unparsed_bytes);
        return true;
      }
      LOG_DEBUG << "ParseReply data=(" << std::string(entry, bulk_array.parsed_size()) << ")";
      backend->buffer()->update_parsed_bytes(bulk_array.parsed_size());

      size_t absent_bulks = bulk_array.absent_bulks();
      LOG_WARN << "RedisMgetCommand::ParseReply absent_bulks=" << absent_bulks
                << ", backend=" << backend;
      if (absent_bulks > 0) {
        absent_bulks_tracker_[backend] = absent_bulks;
        return true;
      }
      if (bulk_array.completed()) {
        LOG_WARN << "RedisMgetCommand::OnBackendReplyReceived ParseReply ok, completed, backend=" << backend;
        backend->set_reply_recv_complete();  // TODO : buffer中，针对memcached协议判断complete的标准，在redis中不适用
      }
    } else {
      redis::Bulk bulk(entry, unparsed_bytes);
      if (bulk.present_size() < 0) {
        LOG_WARN << "RedisMgetCommand::OnBackendReplyReceived ParseReply sub_bulk error, backend=" << backend;
        return false;
      }
      if (bulk.present_size() == 0) {
        LOG_WARN << "RedisMgetCommand::OnBackendReplyReceived ParseReply sub_bulk need more data, backend=" << backend;
        return true;
      }
      backend->buffer()->update_parsed_bytes(bulk.total_size());

      if (--absent_it->second == 0) {
        absent_bulks_tracker_.erase(absent_it);
        if (bulk.completed()) {
          LOG_WARN << "RedisMgetCommand::OnBackendReplyReceived ParseReply sub_bulk ok, completed, backend=" << backend;
          backend->set_reply_recv_complete();
        }
      }
    }
  }

  return true;
}

}

