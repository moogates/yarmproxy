#include "parallel_get_command.h"

#include "base/logging.h"

#include "backend_conn.h"
#include "client_conn.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace mcproxy {

std::atomic_int parallel_get_cmd_count;

const char * GetLineEnd(const char * buf, size_t len);
size_t GetValueBytes(const char * data, const char * end);

ParallelGetCommand::BackendQuery::~BackendQuery() {
}

ParallelGetCommand::ParallelGetCommand(std::shared_ptr<ClientConnection> owner,
                   std::map<ip::tcp::endpoint, std::string>&& endpoint_query_map)
    : Command(owner)
    , last_backend_(nullptr)
{
  for(auto& it : endpoint_query_map) {
    LOG_DEBUG << "ParallelGetCommand ctor, create query ep=" << it.first << " query=" << it.second;
    query_set_.emplace_back(new BackendQuery(it.first, std::move(it.second)));
  }
  LOG_DEBUG << "ParallelGetCommand ctor, connt=" << ++parallel_get_cmd_count;
}

ParallelGetCommand::~ParallelGetCommand() {
  for(auto& query : query_set_) {
    if (query->backend_conn_ == nullptr) {
      LOG_DEBUG << "ParallelGetCommand dtor Release null backend";
    } else {
      LOG_DEBUG << "ParallelGetCommand dtor Release backend";
      context_.backend_conn_pool()->Release(query->backend_conn_);
    }
  }
  LOG_DEBUG << "ParallelGetCommand dtor, cmd=" << this << " connt=" << --parallel_get_cmd_count;
}

void ParallelGetCommand::ForwardQuery(const char *, size_t) {
  DoForwardQuery(nullptr, 0);
}

void ParallelGetCommand::HookOnUpstreamReplyReceived(BackendConn* backend) {
  if (received_reply_backends_.insert(backend).second) {
    if (received_reply_backends_.size() == query_set_.size()) {
      last_backend_ = backend;
    }
  }
}

void ParallelGetCommand::OnForwardQueryFinished(BackendConn* backend, const boost::system::error_code& error) {
  if (error) {
    // TODO : error handling
    LOG_DEBUG << "ParallelGetCommand OnForwardQueryFinished error";
    return;
  }
  LOG_DEBUG << "ParallelGetCommand::OnForwardQueryFinished 转发了当前命令, 等待backend的响应.";
  backend->ReadReply();
}

void ParallelGetCommand::PushWaitingReplyQueue(BackendConn* backend) {
  if (std::find(waiting_reply_queue_.begin(), waiting_reply_queue_.end(), backend)
      == waiting_reply_queue_.end()) {
    waiting_reply_queue_.push_back(backend);
    LOG_DEBUG << "ParallelGetCommand PushWaitingReplyQueue, backend=" << backend
              << " waiting_reply_queue_.size=" << waiting_reply_queue_.size();
  } else {
    LOG_DEBUG << "ParallelGetCommand PushWaitingReplyQueue already in queue, backend=" << backend
              << " waiting_reply_queue_.size=" << waiting_reply_queue_.size();
  }
}

void ParallelGetCommand::OnForwardReplyEnabled() {
  // RotateReplyingBackend();
  LOG_DEBUG << "ParallelGetCommand::OnForwardReplyEnabled cmd=" << this << " old replying_backend_=" << replying_backend_;
  if (waiting_reply_queue_.size() > 0) {
    replying_backend_ = waiting_reply_queue_.front();
    waiting_reply_queue_.pop_front();
    LOG_DEBUG << "ParallelGetCommand::OnForwardReplyEnabled activate ready backend,"
              << " replying_backend_=" << replying_backend_;
    TryForwardReply(replying_backend_);
  } else {
    LOG_DEBUG << "ParallelGetCommand::OnForwardReplyEnabled no ready backend to activate,"
              << " replying_backend_=" << replying_backend_;
    replying_backend_ = nullptr;
  }
}

void ParallelGetCommand::RotateReplyingBackend() {
  if (HasMoreBackend()) {
    OnForwardReplyEnabled();
  } else {
    client_conn_->RotateReplyingCommand();
  }
}

bool ParallelGetCommand::ParseReply(BackendConn* backend) {
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
            backend->buffer()->update_parsed_bytes(sizeof("END\r\n") - 1); // for debug only
            // backend->buffer()->cut_received_tail(sizeof("END\r\n") - 1);
          }
        }
        break;
      } else {
        LOG_WARN << "ParseReply BAD DATA";
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
    BackendConn* backend = query->backend_conn_;
    if (backend == nullptr) {
      backend = context_.backend_conn_pool()->Allocate(query->backend_addr_);
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

