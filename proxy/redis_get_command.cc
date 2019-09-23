#include "redis_get_command.h"

#include "base/logging.h"

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
  backend_endpoint_ = BackendLoactor::Instance().Locate(ba[1].payload_data(), ba[1].payload_size(), "REDIS_bj");
  // LOG_DEBUG << "CreateCommand type=" << ba[0].to_string() << " key=" << ba[1].to_string() << " ep=" << backend_endpoint_;
}

RedisGetCommand::~RedisGetCommand() {
  if (backend_conn_) {
    backend_pool()->Release(backend_conn_);
  }
}

bool RedisGetCommand::WriteQuery() {
  if (!backend_conn_) {
    backend_conn_ = AllocateBackend(backend_endpoint_);
  }
  client_conn_->buffer()->inc_recycle_lock();
  backend_conn_->WriteQuery(cmd_data_, cmd_bytes_);
  return false;
}

void RedisGetCommand::OnBackendError(std::shared_ptr<BackendConn> backend, const char* err_reply) {
  if (has_read_some_reply_) {
    client_conn_->Abort();
    return;
  }
  backend->SetReplyData(err_reply, strlen(err_reply));
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  if (client_conn_->IsFirstCommand(shared_from_this())) {
    TryWriteReply(backend);
  }
}

void RedisGetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
        ErrorCode ec) {
  assert(backend == backend_conn_);
  if (ec != ErrorCode::E_SUCCESS) {
    LOG_WARN << "RedisGetCommand::OnBackendReplyReceived read error, backend=" << backend;
    OnBackendError(backend, "-Backend Read Error\r\n");
    return;
  }

  if (ParseReply(backend) == false) {
    LOG_WARN << "RedisGetCommand::OnBackendReplyReceived protocol error, backend=" << backend;
    OnBackendError(backend, "-Backend Protocol Error\r\n");
    return;
  }

  has_read_some_reply_ = true;
  if (client_conn_->IsFirstCommand(shared_from_this())) {
    TryWriteReply(backend);
  }
  backend->TryReadMoreReply();
}


void RedisGetCommand::OnBackendConnectError(std::shared_ptr<BackendConn> backend) {
  // TODO : use base class impl
  assert(backend == backend_conn_);
  LOG_WARN << "RedisGetCommand::OnBackendConnectError endpoint="
          << backend->remote_endpoint() << " backend=" << backend;
  OnBackendError(backend, "-Backend Connect Error\r\n");
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

