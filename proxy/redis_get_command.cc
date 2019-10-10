#include "redis_get_command.h"

#include "logging.h"

#include "error_code.h"
#include "backend_conn.h"
#include "backend_pool.h"
#include "backend_locator.h"
#include "client_conn.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

RedisGetCommand::RedisGetCommand(std::shared_ptr<ClientConnection> client,
                                 const redis::BulkArray& ba)
    : Command(client)
    , cmd_data_(ba.raw_data())
    , cmd_bytes_(ba.total_size())
{
  backend_endpoint_ = backend_locator()->Locate(
      ba[1].payload_data(), ba[1].payload_size(), ProtocolType::REDIS);
}

RedisGetCommand::~RedisGetCommand() {
  if (backend_conn_) {
    backend_pool()->Release(backend_conn_);
  }
}

bool RedisGetCommand::WriteQuery() {
  assert(!backend_conn_);
  backend_conn_ = AllocateBackend(backend_endpoint_);
  client_conn_->buffer()->inc_recycle_lock();
  backend_conn_->WriteQuery(cmd_data_, cmd_bytes_);
  return false;
}

void RedisGetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
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
}

void RedisGetCommand::RotateReplyingBackend(bool) {
  client_conn_->RotateReplyingCommand();
}

void RedisGetCommand::StartWriteReply() {
  TryWriteReply(backend_conn_);
}

bool RedisGetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  size_t unparsed_bytes = backend->buffer()->unparsed_bytes();

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

