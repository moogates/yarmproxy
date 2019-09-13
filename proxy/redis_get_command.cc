#include "redis_get_command.h"

#include "error_code.h"
#include "logging.h"

#include "backend_conn.h"
#include "backend_pool.h"
#include "backend_locator.h"
#include "client_conn.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

std::atomic_int redis_get_cmd_count;

const char * GetLineEnd(const char * buf, size_t len);
size_t GetValueBytes(const char * data, const char * end);

RedisGetCommand::BackendQuery::~BackendQuery() {
}

RedisGetCommand::RedisGetCommand(std::shared_ptr<ClientConnection> client,
                                 const redis::BulkArray& ba)
    : Command(client) {
  ip::tcp::endpoint ep = BackendLoactor::Instance().GetEndpointByKey(ba[1].payload_data(), ba[1].payload_size(), "REDIS_bj");
  LOG_WARN << "CreateCommand RedisGetCommand key=" << ba[1].to_string()
           << " ep=" << ep;
  query_set_.emplace_back(new BackendQuery(ep, std::string(ba.raw_data(), ba.total_size()))); // TODO : don't copy the query
  LOG_DEBUG << "RedisGetCommand ctor, count=" << ++redis_get_cmd_count;
}

RedisGetCommand::~RedisGetCommand() {
  for(auto& query : query_set_) {
    if (!query->backend_conn_) {
      LOG_DEBUG << "RedisGetCommand dtor Release null backend";
    } else {
      LOG_DEBUG << "RedisGetCommand dtor Release backend";
      backend_pool()->Release(query->backend_conn_);
    }
  }
  LOG_DEBUG << "RedisGetCommand dtor, cmd=" << this << " count=" << --redis_get_cmd_count;
}

void RedisGetCommand::WriteQuery() {
  for(auto& query : query_set_) {
    if (!query->backend_conn_) {
      query->backend_conn_ = AllocateBackend(query->backend_endpoint_);
    }
    LOG_DEBUG << "RedisGetCommand WriteQuery cmd=" << this
              << " backend=" << query->backend_conn_ << ", query=("
              << query->query_line_.substr(0, query->query_line_.size() - 2) << ")";
    query->backend_conn_->WriteQuery(query->query_line_.data(), query->query_line_.size());
  }
}

void RedisGetCommand::TryMarkLastBackend(std::shared_ptr<BackendConn> backend) {
  if (received_reply_backends_.insert(backend).second) {
    if (received_reply_backends_.size() == 1) {
      first_reply_backend_ = backend;
    }
    if (received_reply_backends_.size() == query_set_.size()) {
      last_backend_ = backend;
    }
  }
}

// return : is the backend successfully activated
bool RedisGetCommand::TryActivateReplyingBackend(std::shared_ptr<BackendConn> backend) {
  if (replying_backend_ == nullptr) {
    replying_backend_ = backend;
    LOG_DEBUG << "TryActivateReplyingBackend ok, backend=" << backend << " replying_backend_=" << replying_backend_;
    return true;
  }
  return backend == replying_backend_;
}

void RedisGetCommand::BackendReadyToReply(std::shared_ptr<BackendConn> backend) {
  if (client_conn_->IsFirstCommand(shared_from_this())
      && TryActivateReplyingBackend(backend)) {
    if (backend->finished()) { // 新收的新数据，可能不需要转发，例如收到的刚好是"END\r\n"
      RotateReplyingBackend(backend->recyclable());
    } else {
      TryWriteReply(backend);
    }
  } else {
    // push backend into waiting_reply_queue_ if not existing
    if (std::find(waiting_reply_queue_.begin(), waiting_reply_queue_.end(),
                  backend) == waiting_reply_queue_.end()) {
      waiting_reply_queue_.push_back(backend);
    }
  }
}

void RedisGetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  TryMarkLastBackend(backend);
  if (ec != ErrorCode::E_SUCCESS
      || ParseReply(backend) == false) {
    LOG_WARN << "RedisGetCommand::OnBackendReplyReceived error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  BackendReadyToReply(backend);
  backend->TryReadMoreReply(); // backend 正在read more的时候，不能memmove，不然写回的数据位置会相对漂移
}


void RedisGetCommand::OnBackendConnectError(std::shared_ptr<BackendConn> backend) {
  LOG_DEBUG << "RedisGetCommand::OnBackendConnectError endpoint="
            << backend->remote_endpoint() << " backend=" << backend;
  ++unreachable_backends_;
  TryMarkLastBackend(backend);

  if (backend == last_backend_) {
    static const char END_RN[] = "-Backend Connect Failed\r\n"; // TODO : 统一放置错误码
    backend->SetReplyData(END_RN, sizeof(END_RN) - 1);
    LOG_WARN << "RedisGetCommand::OnBackendConnectError last, endpoint="
            << backend->remote_endpoint() << " backend=" << backend;
  } else {
    LOG_WARN << "RedisGetCommand::OnBackendConnectError not last, endpoint="
            << backend->remote_endpoint() << " backend=" << backend;
  }
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  BackendReadyToReply(backend);
}

void RedisGetCommand::StartWriteReply() {
  NextBackendStartReply();
}

void RedisGetCommand::NextBackendStartReply() {
  LOG_DEBUG << "RedisGetCommand::OnWriteReplyEnabled cmd=" << this
            << " last replying_backend_=" << replying_backend_;
  if (waiting_reply_queue_.size() > 0) {
    auto next_backend = waiting_reply_queue_.front();
    waiting_reply_queue_.pop_front();

    LOG_DEBUG << "RedisGetCommand::OnWriteReplyEnabled activate ready backend,"
              << " backend=" << next_backend;
    if (next_backend->finished()) {
      LOG_DEBUG << "OnWriteReplyEnabled backend=" << next_backend << " empty reply";
      RotateReplyingBackend(next_backend->recyclable());
    } else {
      TryWriteReply(next_backend);
      replying_backend_ = next_backend;
    }
  } else {
    LOG_DEBUG << "RedisGetCommand::OnWriteReplyEnabled no ready backend to activate,"
              << " last_replying_backend_=" << replying_backend_;
    replying_backend_ = nullptr;
  }
}

bool RedisGetCommand::HasUnfinishedBanckends() const {
  LOG_DEBUG << "RedisGetCommand::HasUnfinishedBanckends"
            << " completed_backends_=" << completed_backends_ 
            << " total_backends=" << query_set_.size();
  return unreachable_backends_ + completed_backends_ < query_set_.size();
}

void RedisGetCommand::RotateReplyingBackend(bool success) {
  if (success) {
    ++completed_backends_;
  }
  if (HasUnfinishedBanckends()) {
    LOG_DEBUG << "RedisGetCommand::Rotate to next backend";
    NextBackendStartReply();
  } else {
    LOG_DEBUG << "RedisGetCommand::Rotate to next COMMAND";
    client_conn_->RotateReplyingCommand();
  }
}

bool RedisGetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  size_t unparsed_bytes = backend->buffer()->unparsed_bytes();
  LOG_DEBUG << "RedisGetCommand::ParseReply unparsed_bytes=" << unparsed_bytes
            << " parsed_unreceived=" << backend->buffer()->parsed_unreceived_bytes();

  if (unparsed_bytes == 0) {
    if (backend->buffer()->parsed_unreceived_bytes() == 0) {
      backend->set_reply_recv_complete();
    }
    return true;
  }

  const char * entry = backend->buffer()->unparsed_data();
  redis::Bulk bulk(entry, unparsed_bytes);
  if (bulk.present_size() < 0) {
    return false;
  }
  if (bulk.present_size() == 0) {
    return true;
  }
  LOG_DEBUG << "ParseReply data=(" << std::string(entry, bulk.present_size()) << ")";

  if (bulk.completed()) {
    LOG_DEBUG << "ParseReply bulk completed";
    backend->set_reply_recv_complete();
  } else {
    LOG_DEBUG << "ParseReply bulk not completed";
  }
  LOG_DEBUG << "ParseReply parsed_bytes=" << bulk.total_size();
  backend->buffer()->update_parsed_bytes(bulk.total_size());
  return true;
}

}

