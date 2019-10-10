#include "mc_set_command.h"

#include "logging.h"

#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

MemcachedSetCommand::MemcachedSetCommand(std::shared_ptr<ClientConnection> client,
          const char* buf, size_t cmd_len, size_t* body_bytes) 
    : Command(client) {
  *body_bytes = ParseQuery(buf, cmd_len);
}

size_t MemcachedSetCommand::ParseQuery(const char* cmd_data, size_t cmd_len) {
  // TODO : strict check
  // <command name> <key> <flags> <exptime> <bytes>\r\n
  const char *p = cmd_data;
  while(*(p++) != ' ');

  const char *q = p;
  while(*(++q) != ' ');

  backend_endpoint_ = backend_locator()->Locate(p, q - p,
                          ProtocolType::MEMCACHED);

  p = cmd_data + cmd_len - 2;
  while(*(p - 1) != ' ') {
    --p;
  }
  try {
    return std::atoi(p) + 2; // 2 is length of the ending "\r\n"
  } catch(...) {
    return 0;
  }
}

MemcachedSetCommand::~MemcachedSetCommand() {
  if (backend_conn_) {
    backend_pool()->Release(backend_conn_);
  }
}

bool MemcachedSetCommand::WriteQuery() {
  if (client_conn_->buffer()->parsed_unreceived_bytes() == 0) {
    query_recv_complete_ = true;
  }

  if (connect_error_) {
    assert(backend_conn_);
    LOG_DEBUG << "WriteQuery connect_error_, query_recv_complete_="
             << query_recv_complete_;
    if (query_recv_complete_) {
      if (client_conn_->IsFirstCommand(shared_from_this())) {
        LOG_DEBUG << "RedisSetCommand WriteQuery connect_error_ write reply";
        // query_recv_complete完毕才可以write reply, 因而这里backend一定尚未finished()
        assert(!backend_conn_->finished());
        TryWriteReply(backend_conn_);
      } else {
        LOG_DEBUG << "RedisSetCommand WriteQuery connect_error_ wait to write reply";
      }
    } else {
      LOG_DEBUG << "RedisSetCommand WriteQuery connect_error_ read more query";
      return true; // no callback, try read more query directly
    }
    return false;
  }

  if (!backend_conn_) {
    backend_conn_ = AllocateBackend(backend_endpoint_);
    LOG_DEBUG << "MemcachedSetCommand::WriteQuery backend=" << backend_conn_;
  }

  auto buffer = client_conn_->buffer();
  buffer->inc_recycle_lock();
  backend_conn_->WriteQuery(buffer->unprocessed_data(),
                            buffer->unprocessed_bytes());
  return false;
}

void MemcachedSetCommand::OnBackendRecoverableError(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  auto& err_reply(MemcachedErrorReply(ec));
  backend->SetReplyData(err_reply.data(), err_reply.size());
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  connect_error_ = true;

  if (query_recv_complete_) {
    if (client_conn_->IsFirstCommand(shared_from_this())) {
      // write reply
      TryWriteReply(backend);
    } else {
      // waiting to write reply
    }
  } else {
    // wait for more query data
  }
}

/*
void MemcachedSetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
                                        ErrorCode ec) {
  assert(backend == backend_conn_);
  if (ec == ErrorCode::E_SUCCESS && !ParseReply(backend)) {
    ec = ErrorCode::E_PROTOCOL;
  }
  if (ec != ErrorCode::E_SUCCESS) {
    if (!BackendErrorRecoverable(backend, ec)) {
      client_conn_->Abort();
    } else {
      OnBackendRecoverableError(backend, ec);
    }
    return;
  }

  if (client_conn_->IsFirstCommand(shared_from_this())) {
    // write reply
    TryWriteReply(backend);
  } else {
    // wait to write reply
  }
  backend->TryReadMoreReply();
  return;
  if (ec != ErrorCode::E_SUCCESS || !ParseReply(backend)) {
    LOG_DEBUG << "MemcachedSetCommand::OnBackendReplyReceived err, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  if (client_conn_->IsFirstCommand(shared_from_this())) {
    TryWriteReply(backend);
  }
  backend->TryReadMoreReply();
}
*/

void MemcachedSetCommand::StartWriteReply() {
  // TODO : report error & rotate if connection refused
  if (query_recv_complete_) {
    TryWriteReply(backend_conn_);
  }
}

void MemcachedSetCommand::RotateReplyingBackend(bool) {
  assert(query_recv_complete_);
  client_conn_->RotateReplyingCommand();
}

bool MemcachedSetCommand::query_recv_complete() {
  return query_recv_complete_;
}

bool MemcachedSetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  assert(backend_conn_ == backend);
  const char * entry = backend_conn_->buffer()->unparsed_data();
  const char * p = static_cast<const char *>(memchr(entry, '\n',
                       backend_conn_->buffer()->unparsed_bytes()));
  if (p == nullptr) {
    return true;
  }

  backend_conn_->buffer()->update_parsed_bytes(p - entry + 1);
  backend->set_reply_recv_complete();
  return true;
}

}

