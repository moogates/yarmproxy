#include "mc_basic_command.h"

#include "logging.h"

#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

MemcachedBasicCommand::MemcachedBasicCommand(
    std::shared_ptr<ClientConnection> client, const char* buf, size_t cmd_len) 
    : Command(client) {
  ParseQuery(buf, cmd_len);
}

size_t MemcachedBasicCommand::ParseQuery(const char* cmd_data, size_t cmd_len) {
  // <command> <key> [noreply]\r\n
  const char *p = cmd_data;
  while(*(p++) != ' ');

  const char *q = p;
  while(*(++q) != ' ' && *q != '\r');

  backend_endpoint_ = backend_locator()->Locate(p, q - p,
                          ProtocolType::MEMCACHED);
  return 0; // 2 is length of the ending "\r\n"
}

MemcachedBasicCommand::~MemcachedBasicCommand() {
  if (backend_conn_) {
    backend_pool()->Release(backend_conn_);
  }
}

bool MemcachedBasicCommand::WriteQuery() {
  assert(backend_conn_ == nullptr);
  if (!backend_conn_) {
    backend_conn_ = AllocateBackend(backend_endpoint_);
    LOG_DEBUG << "MemcachedBasicCommand::WriteQuery backend=" << backend_conn_;
  }

  auto buffer = client_conn_->buffer();
  buffer->inc_recycle_lock();
  backend_conn_->WriteQuery(buffer->unprocessed_data(),
                            buffer->unprocessed_bytes());
  return false;
}
/*
void MemcachedBasicCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
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
    LOG_WARN << "MemcachedBasicCommand::OnBackendReplyReceived err, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  if (client_conn_->IsFirstCommand(shared_from_this())) {
    TryWriteReply(backend);
  }
  backend->TryReadMoreReply();
}
*/
void MemcachedBasicCommand::StartWriteReply() {
  // TODO : report error & rotate if connection refused
  TryWriteReply(backend_conn_);
}

void MemcachedBasicCommand::RotateReplyingBackend(bool) {
  client_conn_->RotateReplyingCommand();
}

bool MemcachedBasicCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
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

