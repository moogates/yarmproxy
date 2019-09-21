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

void RedisGetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
        ErrorCode ec) {
  assert(backend == backend_conn_);

  if (ec != ErrorCode::E_SUCCESS
      || ParseReply(backend) == false) {
    LOG_WARN << "RedisGetCommand::OnBackendReplyReceived error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  if (client_conn_->IsFirstCommand(shared_from_this())) {
    TryWriteReply(backend);
  }
  backend->TryReadMoreReply();
}


void RedisGetCommand::OnBackendConnectError(std::shared_ptr<BackendConn> backend) {
  // TODO : use base class impl
  assert(backend == backend_conn_);

  static const char END_RN[] = "-Backend Connect Failed\r\n"; // TODO : 统一放置错误码
  backend->SetReplyData(END_RN, sizeof(END_RN) - 1);
  LOG_WARN << "RedisGetCommand::OnBackendConnectError endpoint="
          << backend->remote_endpoint() << " backend=" << backend;

  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  if (client_conn_->IsFirstCommand(shared_from_this())) {
    TryWriteReply(backend);
  }
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

