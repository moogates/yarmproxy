#include "parallel_get_command.h"

#include "error_code.h"
#include "logging.h"

#include "backend_conn.h"
#include "client_conn.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

std::atomic_int parallel_get_cmd_count;

const char * GetLineEnd(const char * buf, size_t len);
size_t GetValueBytes(const char * data, const char * end);

ParallelGetCommand::BackendQuery::~BackendQuery() {
}

ParallelGetCommand::ParallelGetCommand(std::shared_ptr<ClientConnection> client, const std::string& original_header,
                   std::map<ip::tcp::endpoint, std::string>&& endpoint_query_map)
    : Command(client, original_header)
    , last_backend_(nullptr)
{
  for(auto& it : endpoint_query_map) {
    LOG_DEBUG << "ParallelGetCommand ctor, create query ep=" << it.first << " query=" << it.second;
    query_set_.emplace_back(new BackendQuery(it.first, std::move(it.second)));
  }
  LOG_DEBUG << "ParallelGetCommand ctor, query_set_.size=" << query_set_.size() << " count=" << ++parallel_get_cmd_count;
}

ParallelGetCommand::~ParallelGetCommand() {
  for(auto& query : query_set_) {
    if (!query->backend_conn_) {
      LOG_DEBUG << "ParallelGetCommand dtor Release null backend";
    } else {
      LOG_DEBUG << "ParallelGetCommand dtor Release backend";
      context().backend_conn_pool()->Release(query->backend_conn_);
    }
  }
  LOG_DEBUG << "ParallelGetCommand dtor, cmd=" << this << " count=" << --parallel_get_cmd_count;
}

void ParallelGetCommand::ForwardQuery(const char *, size_t) {
  DoForwardQuery(nullptr, 0);
}

void ParallelGetCommand::HookOnUpstreamReplyReceived(std::shared_ptr<BackendConn> backend) {
  if (received_reply_backends_.insert(backend).second) {
    // if (unreachable_backends_ + received_reply_backends_.size() == query_set_.size()) {
    if (received_reply_backends_.size() == query_set_.size()) {
      last_backend_ = backend;
    }
  }
}

void ParallelGetCommand::OnBackendConnectError(std::shared_ptr<BackendConn> backend) {
  LOG_WARN << "ParallelGetCommand::OnBackendConnectError endpoint=" << backend->remote_endpoint()
           << " backend=" << backend;
  ++unreachable_backends_;
  // backend->Close();

  // pipeline的情况下，要排队写
  //
  HookOnUpstreamReplyReceived(backend);

  if (HasMoreBackend()) {
    backend->set_reply_complete();
    backend->set_no_recycle();
    if (backend == replying_backend_) {
      // ++unreachable_backends_;
      RotateReplyingBackend();
    }
    LOG_WARN << "ParallelGetCommand::OnBackendConnectError silence, endpoint=" << backend->remote_endpoint()
           << " backend=" << backend;
    return;
  }

  if (completed_backends_ > 0) {
    if (backend == last_backend_) {
      if (client_conn_->IsFirstCommand(shared_from_this()) && TryActivateReplyingBackend(backend)) {
        static const char BACKEND_ERROR[] = "inst_END\r\n"; // TODO : 统一放置错误码
        backend->SetReplyData(BACKEND_ERROR, sizeof(BACKEND_ERROR) - 1);
        backend->set_reply_complete(); // TODO : reply complete can't be the only standard to recycle
        backend->set_no_recycle();

        TryForwardReply(backend);
      } else {
        static const char BACKEND_ERROR[] = "wait_END\r\n"; // TODO : 统一放置错误码
        backend->SetReplyData(BACKEND_ERROR, sizeof(BACKEND_ERROR) - 1);
        backend->set_reply_complete();
        backend->set_no_recycle();
 
        PushWaitingReplyQueue(backend);
      }
    } else {
        backend->set_reply_complete();
        backend->set_no_recycle();
        PushWaitingReplyQueue(backend);
    }
  } else {
  if (client_conn_->IsFirstCommand(shared_from_this()) && TryActivateReplyingBackend(backend)) {
    static const char BACKEND_ERROR[] = "BACKEND_CONNECTION_REFUSED\r\n"; // TODO : 统一放置错误码
    backend->SetReplyData(BACKEND_ERROR, sizeof(BACKEND_ERROR) - 1);
    backend->set_reply_complete(); // TODO : reply complete can't be the only standard to recycle
        backend->set_no_recycle();

    TryForwardReply(backend);
  } else {
    static const char BACKEND_ERROR[] = "wait_BACKEND_CONNECTION_REFUSED\r\n"; // TODO : 统一放置错误码
    backend->SetReplyData(BACKEND_ERROR, sizeof(BACKEND_ERROR) - 1);
    backend->set_reply_complete();
        backend->set_no_recycle();

    PushWaitingReplyQueue(backend);
  }
  }
}

void ParallelGetCommand::OnForwardQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    // client_conn_->Abort(); // TODO : 模拟Abort
    if (ec == ErrorCode::E_CONNECT) {
      OnBackendConnectError(backend);
    } else {
      client_conn_->Abort();
      LOG_INFO << "WriteCommand OnForwardQueryFinished error";
    }
    return;
  }
  LOG_DEBUG << "ParallelGetCommand::OnForwardQueryFinished 转发了当前命令, 等待backend的响应.";
  backend->ReadReply();
}

void ParallelGetCommand::PushWaitingReplyQueue(std::shared_ptr<BackendConn> backend) {
  if (std::find(waiting_reply_queue_.begin(), waiting_reply_queue_.end(), backend)
      == waiting_reply_queue_.end()) {
    waiting_reply_queue_.push_back(backend);
    LOG_DEBUG << "ParallelGetCommand PushWaitingReplyQueue, backend=" << backend.get()
              << " waiting_reply_queue_.size=" << waiting_reply_queue_.size();
  } else {
    LOG_DEBUG << "ParallelGetCommand PushWaitingReplyQueue already in queue, backend=" << backend.get()
              << " waiting_reply_queue_.size=" << waiting_reply_queue_.size();
  }
}

void ParallelGetCommand::OnForwardReplyEnabled() {
  // RotateReplyingBackend();
  LOG_DEBUG << "ParallelGetCommand::OnForwardReplyEnabled cmd=" << this << " old replying_backend_=" << replying_backend_.get();
  if (waiting_reply_queue_.size() > 0) {
    // replying_backend_ = waiting_reply_queue_.front();
    auto backend = waiting_reply_queue_.front();
    waiting_reply_queue_.pop_front();
    LOG_DEBUG << "ParallelGetCommand::OnForwardReplyEnabled activate ready backend,"
              << " backend=" << backend;
    if (backend->reply_complete() && backend->buffer()->unprocessed_bytes() == 0) {
      ++completed_backends_;
      LOG_DEBUG << "OnForwardReplyEnabled backend=" << backend << " empty reply";
      RotateReplyingBackend();
    } else {
      TryForwardReply(backend);
      replying_backend_ = backend;
    }
  } else {
    LOG_DEBUG << "ParallelGetCommand::OnForwardReplyEnabled no ready backend to activate,"
              << " replying_backend_=" << replying_backend_;
    replying_backend_ = nullptr;
  }
}

bool ParallelGetCommand::HasMoreBackend() const { // rename -> HasUnfinishedBanckends()
  LOG_WARN << "ParallelGetCommand::HasMoreBackend "
            << " unreachable_backends_=" << unreachable_backends_
            << " completed_backends_=" << completed_backends_ 
            << " total_backends=" << query_set_.size();
  return unreachable_backends_ + completed_backends_ < query_set_.size(); // NOTE: 注意这里要
}

void ParallelGetCommand::RotateReplyingBackend() {
  if (HasMoreBackend()) {
    LOG_DEBUG << "ParallelGetCommand::Rotate to next backend";
    OnForwardReplyEnabled();
  } else {
    LOG_DEBUG << "ParallelGetCommand::Rotate to next COMMAND";
    client_conn_->RotateReplyingCommand();
  }
}

bool ParallelGetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  bool valid = true;
  while(backend->buffer()->unparsed_bytes() > 0) {
    const char * entry = backend->buffer()->unparsed_data();
    size_t unparsed_bytes = backend->buffer()->unparsed_bytes();
    const char * p = GetLineEnd(entry, unparsed_bytes);
    if (p == nullptr) {
      LOG_DEBUG << "ParseReply no enough data for parsing, please read more"
                << " bytes=" << backend->buffer()->unparsed_bytes();
      return true;
    }

    if (entry[0] == 'V') {
      // "VALUE <key> <flag> <bytes>\r\n"
      size_t body_bytes = GetValueBytes(entry, p);
      size_t entry_bytes = p - entry + 1 + body_bytes + 2;
      LOG_DEBUG << "ParseReply VALUE data, backend=" << backend << " bytes=" << std::min(unparsed_bytes, entry_bytes)
                << " data=(" << std::string(entry, std::min(unparsed_bytes, entry_bytes)) << ")";
      backend->buffer()->update_parsed_bytes(entry_bytes);
      // break; // 这里如果break, 则每次转发一条，only for test
    } else {
      // "END\r\n"
      if (strncmp("END\r\n", entry, sizeof("END\r\n") - 1) == 0) {
        if (backend->buffer()->unparsed_bytes() != (sizeof("END\r\n") - 1)) {
          valid = false;
          LOG_DEBUG << "ParseReply END not really end! backend=" << backend;
        } else {
          backend->set_reply_complete();
          if (backend == last_backend_) {
            LOG_DEBUG << "ParseReply END is really end, is last, backend=" << backend;
            backend->buffer()->update_parsed_bytes(sizeof("END\r\n") - 1);
          } else {
            LOG_DEBUG << "ParseReply END is really end, is not last, backend=" << backend;
            // backend->buffer()->update_parsed_bytes(sizeof("END\r\n") - 1); // for debug only
            backend->buffer()->cut_received_tail(sizeof("END\r\n") - 1);
          }
        }
        break;
      } else {
        LOG_WARN << "ParseReply BAD DATA, line=[" << std::string(entry, p - entry) << "]";
        // TODO : ERROR
        valid = false;
        break;
      }
    }
  }
  return valid;
}

void ParallelGetCommand::DoForwardQuery(const char *, size_t) {
  for(auto& query : query_set_) {
    std::shared_ptr<BackendConn> backend = query->backend_conn_;
    if (!backend) {
      backend = context().backend_conn_pool()->Allocate(query->backend_addr_);
      backend->SetReadWriteCallback(WeakBind(&Command::OnForwardQueryFinished, backend),
                                 WeakBind(&Command::OnUpstreamReplyReceived, backend));
      query->backend_conn_ = backend;
      LOG_DEBUG << "ParallelGetCommand ForwardQuery cmd=" << this << " allocated backend=" << backend << " query=("
                << query->query_line_.substr(0, query->query_line_.size() - 2) << ")";
    }
    LOG_DEBUG << "ParallelGetCommand ForwardQuery cmd=" << this << " backend=" << backend << ", query=("
              << query->query_line_.substr(0, query->query_line_.size() - 2) << ")";
    backend->ForwardQuery(query->query_line_.data(), query->query_line_.size(), false);
  }
}

}

