#include "set_command.h"

#include "base/logging.h"

#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

SetCommand::SetCommand(std::shared_ptr<ClientConnection> client,
          const char* buf, size_t cmd_len, size_t* body_bytes) 
    : Command(client) {
  *body_bytes = ParseQuery(buf, cmd_len);
}

size_t SetCommand::ParseQuery(const char* cmd_data, size_t cmd_len) {
  // <command name> <key> <flags> <exptime> <bytes>\r\n
  const char *p = cmd_data;
  while(*(p++) != ' ');

  const char *q = p;
  while(*(++q) != ' ');

  backend_endpoint_ = BackendLoactor::Instance().Locate(p, q - p,
                          ProtocolType::MEMCACHED);

  p = cmd_data + cmd_len - 2;
  while(*(p - 1) != ' ') {
    --p;
  }
  return std::atoi(p) + 2; // 2 is lenght of the ending "\r\n"
}

SetCommand::~SetCommand() {
  if (backend_conn_) {
    backend_pool()->Release(backend_conn_);
  }
}

bool SetCommand::WriteQuery() {
  if (client_conn_->buffer()->parsed_unreceived_bytes() == 0) {
    // LOG_WARN << "RedisSetCommand WriteQuery query_recv_complete_ is true";
    query_recv_complete_ = true;
  }

  if (!backend_conn_) {
    backend_conn_ = AllocateBackend(backend_endpoint_);
    LOG_DEBUG << "SetCommand::WriteQuery backend=" << backend_conn_;
  }

  auto buffer = client_conn_->buffer();
  buffer->inc_recycle_lock();
  backend_conn_->WriteQuery(buffer->unprocessed_data(),
                            buffer->unprocessed_bytes());
  return false;
}

void SetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
                                        ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS || !ParseReply(backend)) {
    LOG_WARN << "SetCommand::OnBackendReplyReceived err, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  if (client_conn_->IsFirstCommand(shared_from_this())) {
    TryWriteReply(backend);
  }
  backend->TryReadMoreReply();
}

void SetCommand::StartWriteReply() {
  // TODO : report error & rotate if connection refused
  TryWriteReply(backend_conn_);
}

void SetCommand::RotateReplyingBackend(bool) {
  client_conn_->RotateReplyingCommand();
}

bool SetCommand::query_recv_complete() {
  return query_recv_complete_;
}

bool SetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
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

