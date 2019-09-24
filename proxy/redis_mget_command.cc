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

// TODO:
// 1. MGET 支持不完整命令
// 2. MGET 应当更早地释放backend conn, 而不是每个subquery一直持有一个连接

std::atomic_int redis_mget_cmd_count;

bool RedisMgetCommand::ParseQuery(const redis::BulkArray& ba) {
  ip::tcp::endpoint last_endpoint;
  const char* current_bulks_data = nullptr;
  size_t current_bulks_count = 0;
  size_t current_bulks_bytes = 0;

  for(size_t i = 1; i < ba.total_bulks(); ++i) {
    const redis::Bulk& bulk = ba[i];
    ip::tcp::endpoint current_endpoint = BackendLoactor::Instance().Locate(bulk.payload_data(), bulk.payload_size(), "REDIS_bj");
    // ip::tcp::endpoint current_endpoint = BackendLoactor::Instance().Locate(bulk.payload_data(), bulk.payload_size());

    LOG_DEBUG << "ParseQuery key=" << bulk.to_string() << " ep=" << current_endpoint
              << " last_ep=" << last_endpoint;

    if (current_endpoint == last_endpoint) {
      ++current_bulks_count;
      current_bulks_bytes += bulk.total_size();
      LOG_DEBUG << "ParseQuery subquery=[" << std::string(bulk.raw_data(), bulk.total_size()) << "]";
    } else {
      if (current_bulks_data != nullptr) {
        std::string subquery(redis::BulkArray::SerializePrefix(current_bulks_count + 1));
        subquery.append("$4\r\nmget\r\n"); // (ba[0].raw_data(), ba[0].total_size())
        subquery.append(current_bulks_data, current_bulks_bytes);

        LOG_DEBUG << "ParseQuery create subquery ep=" << last_endpoint
              << " bulks_count=" << current_bulks_count
              << " query=(" << subquery
              << ") current_key=" << bulk.to_string() << "(not included)";

        // endpoint_keys_list->emplace_back(last_endpoint, std::move(subquery));
        subqueries_.emplace_back(new BackendQuery(last_endpoint, std::move(subquery), current_bulks_count));

        current_bulks_data = nullptr;
      }
      last_endpoint = current_endpoint;
      current_bulks_data = bulk.raw_data();
      current_bulks_count = 1;
      current_bulks_bytes = bulk.total_size();
      LOG_DEBUG << "ParseQuery subquery=[" << std::string(bulk.raw_data(), bulk.total_size()) << "]";
    }
  }

  if (current_bulks_data != nullptr) {
    std::string subquery(redis::BulkArray::SerializePrefix(current_bulks_count + 1));
    subquery.append("$4\r\nmget\r\n"); // (ba[0].raw_data(), ba[0].total_size())
    subquery.append(current_bulks_data, current_bulks_bytes);
    // endpoint_keys_list->emplace_back(last_endpoint, std::move(subquery));
    subqueries_.emplace_back(new BackendQuery(last_endpoint, std::move(subquery), current_bulks_count));

    LOG_DEBUG << "ParseQuery create last subquery ep=" << last_endpoint
              << " bulks_count=" << current_bulks_count;
  }
  return true;
}

RedisMgetCommand::RedisMgetCommand(std::shared_ptr<ClientConnection> client,
                                   const redis::BulkArray& ba)
    : Command(client)
    , reply_prefix_(redis::BulkArray::SerializePrefix(ba.total_bulks() - 1))
{
  ParseQuery(ba);

  LOG_DEBUG << "RedisMgetCommand ctor, cmd=" << this << " count=" << ++redis_mget_cmd_count
            << " reply_prefix_=" << reply_prefix_;
}

RedisMgetCommand::~RedisMgetCommand() {
  for(auto& query : subqueries_) {
    if (!query->backend_conn_) {
      LOG_DEBUG << "RedisMgetCommand dtor Release null backend";
    } else {
      LOG_DEBUG << "RedisMgetCommand dtor Release backend";
      backend_pool()->Release(query->backend_conn_);
    }
  }
  LOG_DEBUG << "RedisMgetCommand dtor, cmd=" << this << " count=" << --redis_mget_cmd_count;
}

bool RedisMgetCommand::WriteQuery() {
  for(auto& query : subqueries_) {
    if (!query->backend_conn_) {
      query->backend_conn_ = AllocateBackend(query->backend_endpoint_);

      // redis mget special
      waiting_reply_queue_.push_back(query->backend_conn_);
      backend_subqueries_.emplace(query->backend_conn_, query);
    }
    LOG_DEBUG << "RedisMgetCommand WriteQuery cmd=" << this
              << " backend=" << query->backend_conn_ << ", query=("
              << query->query_data_.substr(0, query->query_data_.size() - 2) << ")";
    query->backend_conn_->WriteQuery(query->query_data_.data(),
                                     query->query_data_.size());
  }
  if (client_conn_->IsFirstCommand(shared_from_this())) {
    StartWriteReply();
  } else {
    LOG_DEBUG << "RedisMgetCommand no StartWriteReply cmd=" << this;
  }
  return false;
}

void RedisMgetCommand::OnWriteQueryFinished(std::shared_ptr<BackendConn> backend,
                                   ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    if (ec == ErrorCode::E_CONNECT) {
      LOG_DEBUG << "OnWriteQueryFinished connect error, backend=" << backend
               << " ep=" << backend->remote_endpoint();
      OnBackendConnectError(backend);
    } else {
      LOG_DEBUG << "OnWriteQueryFinished error, backend=" << backend
               << " ep=" << backend->remote_endpoint();
      client_conn_->Abort();
    }
    return;
  }
  LOG_DEBUG << "OnWriteQueryFinished ok, backend=" << backend;
  backend->ReadReply(); // TODO : 这里做了简化，要求mget读取完整的指令才开始处理
}

void RedisMgetCommand::OnWriteReplyFinished(std::shared_ptr<BackendConn> backend,
                                   ErrorCode ec) {
  LOG_DEBUG << "RedisMgetCommand OnWriteReplyFinished, backend=" << backend << " ec=" << int(ec);
  if (backend == nullptr) {
    if (ec != ErrorCode::E_SUCCESS) {
      client_conn_->Abort();
      return;
    }
    set_reply_prefix_complete();

    LOG_DEBUG << "RedisMgetCommand ReplyPrefix complete, backend=" << backend;
    NextBackendStartReply();
    return;
  }

  Command::OnWriteReplyFinished(backend, ec);
}

void RedisMgetCommand::TryMarkLastBackend(std::shared_ptr<BackendConn> backend) {
  if (received_reply_backends_.insert(backend).second) {
    if (received_reply_backends_.size() == subqueries_.size()) {
      last_backend_ = backend;
    }
  }
}

// return : is the backend successfully activated
bool RedisMgetCommand::TryActivateReplyingBackend(std::shared_ptr<BackendConn> backend) {
  if (!reply_prefix_complete()) {
    return false;
  }
  assert(waiting_reply_queue_.size() > 0);
  if (backend != waiting_reply_queue_.front()) {
    LOG_DEBUG << "TryActivateReplyingBackend backend=" << backend << " runs too fast,"
             << " waiting_reply_queue_.size=" << waiting_reply_queue_.size()
             << " waiting_reply_queue_.front=" << waiting_reply_queue_.front();
    return false;
  }

  if (replying_backend_ == nullptr) {
    replying_backend_ = backend;
    LOG_DEBUG << "TryActivateReplyingBackend ok, backend=" << backend
              << " replying_backend_=" << replying_backend_;
    return true;
  }
  return backend == replying_backend_;
}

void RedisMgetCommand::BackendReadyToReply(std::shared_ptr<BackendConn> backend) {
  if (!client_conn_->IsFirstCommand(shared_from_this())) {
    return;
  }
    if (backend != replying_backend_) {
      if (!TryActivateReplyingBackend(backend)) {
        return;
      }
      assert(backend == waiting_reply_queue_.front());
      waiting_reply_queue_.pop_front();
    }

    assert(!backend->finished());
    LOG_DEBUG << "RedisMgetCommand::BackendReadyToReply TryWriteReply, backend=" << backend;
    TryWriteReply(backend);
}

void RedisMgetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  TryMarkLastBackend(backend);
  if (ec != ErrorCode::E_SUCCESS
      || ParseReply(backend) == false) {
    LOG_DEBUG << "RedisMgetCommand::OnBackendReplyReceived error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  LOG_DEBUG << "RedisMgetCommand::OnBackendReplyReceived, endpoint="
           << backend->remote_endpoint() << " backend=" << backend;
  BackendReadyToReply(backend);
  backend->TryReadMoreReply();
}

static std::string ErrorReplyBody(size_t keys) {
  std::ostringstream oss;
  // oss << "*" << keys << "\r\n";
  for(size_t i = 0; i < keys; ++i) {
    oss << "$-1\r\n";
  }
  return oss.str();
}

void RedisMgetCommand::OnBackendConnectError(std::shared_ptr<BackendConn> backend) {
  ++unreachable_backends_;
  TryMarkLastBackend(backend);

  LOG_DEBUG << "RedisMgetCommand::OnBackendConnectError backend=" << backend
           << " key_count=" << backend_subqueries_[backend]->key_count_;
  std::string err_reply = ErrorReplyBody(backend_subqueries_[backend]->key_count_);
  backend->SetReplyData(err_reply.data(), err_reply.size());

  backend->set_reply_recv_complete();
  backend->set_no_recycle();
  BackendReadyToReply(backend);
}

void RedisMgetCommand::StartWriteReply() {
  LOG_DEBUG << "RedisMgetCommand::StartWriteReply cmd=" << this;
  client_conn_->WriteReply(reply_prefix_.data(), reply_prefix_.size(),
          WeakBind(&Command::OnWriteReplyFinished, nullptr));
}

void RedisMgetCommand::NextBackendStartReply() {
  LOG_DEBUG << "RedisMgetCommand::NextBackendStartReply cmd=" << this
            << " last_replying_backend_=" << replying_backend_;
  if (!reply_prefix_complete()) {
    return;
  }

  assert(waiting_reply_queue_.size() > 0);

  auto next_backend = waiting_reply_queue_.front();
  if (next_backend->buffer()->unprocessed_bytes() > 0) { // TODO : is this condition enough for ready?
    waiting_reply_queue_.pop_front();
    LOG_DEBUG << "RedisMgetCommand::NextBackendStartReply activate ready backend,"
            << " backend=" << next_backend;
    TryWriteReply(next_backend);
    replying_backend_ = next_backend;
  } else {
    LOG_DEBUG << "RedisMgetCommand::NextBackendStartReply activate but next backend not ready,"
            << " backend=" << next_backend;
    replying_backend_ = nullptr;
  }
}

bool RedisMgetCommand::HasUnfinishedBanckends() const {
  LOG_DEBUG << "RedisMgetCommand::HasUnfinishedBanckends"
            << " unreachable_backends_=" << unreachable_backends_
            << " completed_backends_=" << completed_backends_ 
            << " total_backends=" << subqueries_.size();
  return unreachable_backends_ + completed_backends_ < subqueries_.size();
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
    if (backend->buffer()->parsed_unreceived_bytes() == 0 &&
        backend_subqueries_[backend]->reply_absent_bulks_ == 0) {
      LOG_DEBUG << "RedisMgetCommand::ParseReply last item all data received";
      backend->set_reply_recv_complete();
    }
    return true;
  }

  while(backend->buffer()->unparsed_bytes() > 0) {
    const char * entry = backend->buffer()->unparsed_data();
    size_t unparsed_bytes = backend->buffer()->unparsed_bytes();

    // auto absent_it = absent_bulks_tracker_.find(backend);
    size_t& absent_bulks = backend_subqueries_[backend]->reply_absent_bulks_;
    if (absent_bulks == 0) {
      redis::BulkArray bulk_array(entry, unparsed_bytes);
      if (bulk_array.parsed_size() < 0) {
        LOG_DEBUG << "RedisMgetCommand::OnBackendReplyReceived ParseReply error, backend=" << backend;
        return false;
      }
      if (bulk_array.parsed_size() == 0 || bulk_array.present_bulks() == 0) {
        LOG_DEBUG << "RedisMgetCommand ParseReply need more data, backend=" << backend;
        return true;
      }
      backend->buffer()->update_parsed_bytes(bulk_array.parsed_size());
      LOG_WARN << "RedisMgetCommand ParseReply skip prefix data="
               << std::string(bulk_array.raw_data(), bulk_array[0].raw_data() - bulk_array.raw_data() - 2)
               << ", backend=" << backend;
      backend->buffer()->update_processed_offset(bulk_array[0].raw_data() - bulk_array.raw_data());

      absent_bulks = bulk_array.absent_bulks();
      if (absent_bulks > 0) {
        // absent_bulks_tracker_[backend] = absent_bulks;
        return true;
      }
      if (bulk_array.completed()) {
        LOG_DEBUG << "RedisMgetCommand ParseReply completed, backend=" << backend;
        backend->set_reply_recv_complete();  // TODO : buffer中，针对memcached协议判断complete的标准，在redis中不适用
      }
    } else {
      redis::Bulk bulk(entry, unparsed_bytes);
      if (bulk.present_size() < 0) {
        return false;
      }
      if (bulk.present_size() == 0) {
        return true;
      }
      backend->buffer()->update_parsed_bytes(bulk.total_size());

      if (--absent_bulks == 0) {
        if (bulk.completed()) {
          LOG_DEBUG << "RedisMgetCommand ParseReply absent bulks completed, backend=" << backend;
          backend->set_reply_recv_complete();
        }
      }
    }
  }

  return true;
}

}

