#include "redis_basic_command.h"

#include "logging.h"

#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

RedisBasicCommand::RedisBasicCommand(std::shared_ptr<ClientConnection> client,
                                     const redis::BulkArray& ba)
    : Command(client, ProtocolType::REDIS) {
  auto ep = backend_locator()->Locate(
      ba[1].payload_data(), ba[1].payload_size(), ProtocolType::REDIS);
  replying_backend_ = backend_pool()->Allocate(ep);
}

RedisBasicCommand::~RedisBasicCommand() {
  if (replying_backend_) {
    backend_pool()->Release(replying_backend_);
  }
}

/*
bool RedisBasicCommand::WriteQuery() {
  assert(replying_backend_);
  update_check_query_recv_complete();

  if (first_write_query_) {
    replying_backend_->SetReadWriteCallback(
        WeakBind(&Command::OnWriteQueryFinished, replying_backend_),
        WeakBind(&Command::OnBackendReplyReceived, replying_backend_));
    first_write_query_ = false;
  } else {
    assert(false);
  }

  auto buffer = client_conn_->buffer();
  buffer->inc_recycle_lock();
  replying_backend_->WriteQuery(buffer->unprocessed_data(),
                            buffer->unprocessed_bytes());
  return false;
}
*/
/*
void RedisBasicCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
                                        ErrorCode ec) {
  assert(backend == replying_backend_);
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
    LOG_WARN << "RedisBasicCommand::OnBackendReplyReceived err, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  if (client_conn_->IsFirstCommand(shared_from_this())) {
    TryWriteReply(backend);
  }
  backend->TryReadMoreReply();
}
*/
void RedisBasicCommand::StartWriteReply() {
  // TODO : report error & rotate if connection refused
  TryWriteReply(replying_backend_);
}

void RedisBasicCommand::RotateReplyingBackend(bool) {
  client_conn_->RotateReplyingCommand();
}

bool RedisBasicCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  size_t unparsed = backend->buffer()->unparsed_bytes();
  assert(unparsed > 0);
  const char * entry = backend->buffer()->unparsed_data();
  if (entry[0] != ':' && entry[0] != '+' && entry[0] != '-' &&
      entry[0] != '$') { // TODO : fix the $ reply (aka. bulk)
    LOG_WARN << "RedisBasicCommand ParseReply error ["
             << std::string(entry, unparsed) << "]";
    return false;
  }

  const char * p = static_cast<const char *>(memchr(entry, '\n', unparsed));
  if (p == nullptr) {
    return true;
  }
  if (entry[0] == '$' && entry[1] != '-') {
    p = static_cast<const char *>(memchr(p + 1, '\n', entry + unparsed - p));
    if (p == nullptr) {
      return true;
    }
  }

  backend->buffer()->update_parsed_bytes(p - entry + 1);
  LOG_DEBUG << "RedisBasicCommand ParseReply complete, resp.size=" << p - entry + 1
            << " backend=" << backend;
  backend->set_reply_recv_complete();
  return true;
}

}

