#include "redis_set_command.h"

#include "logging.h"
#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "worker_pool.h"

#include "redis_protocol.h"

namespace yarmproxy {

const char * GetLineEnd(const char * buf, size_t len);

std::atomic_int redis_set_cmd_count;

RedisSetCommand::RedisSetCommand(std::shared_ptr<ClientConnection> client, const redis::BulkArray& ba)
    : Command(client)
    , unparsed_bulks_(ba.absent_bulks())
{
  backend_endpoint_ = BackendLoactor::Instance().GetEndpointByKey(ba[1].payload_data(), ba[1].payload_size(), "REDIS_bj");
  LOG_WARN << "RedisSetCommand key=" << ba[1].to_string() << " ep=" << backend_endpoint_
            << " ctor " << ++redis_set_cmd_count;
}

RedisSetCommand::~RedisSetCommand() {
  if (backend_conn_) {
    backend_pool()->Release(backend_conn_);
  }
  LOG_DEBUG << "RedisSetCommand dtor " << --redis_set_cmd_count;
}

void RedisSetCommand::WriteQuery() {
  if (!backend_conn_) {
    backend_conn_ = AllocateBackend(backend_endpoint_);
    LOG_DEBUG << "RedisSetCommand::WriteQuery allocated backend=" << backend_conn_;
  }

  client_conn_->buffer()->inc_recycle_lock();
  backend_conn_->WriteQuery(client_conn_->buffer()->unprocessed_data(),
          client_conn_->buffer()->unprocessed_bytes());
}

void RedisSetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS || !ParseReply(backend)) {
    LOG_WARN << "Command::OnBackendReplyReceived error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  // 判断是否最靠前的command, 是才可以转发
  if (client_conn_->IsFirstCommand(shared_from_this())) {
    TryWriteReply(backend);
  }
  backend->TryReadMoreReply(); // backend 正在read more的时候，不能memmove，不然写回的数据位置会相对漂移
}

void RedisSetCommand::StartWriteReply() {
  LOG_DEBUG << "StartWriteReply TryWriteReply backend_conn_=" << backend_conn_;
  // TODO : if connection refused, should report error & rotate
  TryWriteReply(backend_conn_);
}

void RedisSetCommand::RotateReplyingBackend(bool) {
  client_conn_->RotateReplyingCommand();
}

/*
void RedisSetCommand::OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  assert(backend == backend_conn_);
  if (ec != ErrorCode::E_SUCCESS) {
    if (ec == ErrorCode::E_CONNECT) {
      LOG_WARN << "RedisSetCommand OnWriteQueryFinished connection_refused, endpoint=" << backend->remote_endpoint()
               << " backend=" << backend;
      OnBackendConnectError(backend);
    } else {
      client_conn_->Abort();
      LOG_WARN << "RedisSetCommand OnWriteQueryFinished error";
    }
    return;
  }
  assert(backend == backend_conn_);
  client_conn_->buffer()->dec_recycle_lock();

  // TODO : 从这里来看，应该是在write query完成之前，禁止client conn进一步的读取
  if (client_conn_->buffer()->parsed_unreceived_bytes() > 0) {
    client_conn_->TryReadMoreQuery();
  } else {
    backend_conn_->ReadReply();
  }
}
*/

bool RedisSetCommand::QueryParsingComplete() {
  LOG_WARN << "RedisSetCommand::QueryParsingComplete unparsed_bulks_=" << unparsed_bulks_;
  return unparsed_bulks_ == 0;
}

bool RedisSetCommand::ParseIncompleteQuery() {
  ReadBuffer* buffer = client_conn_->buffer();
  while(unparsed_bulks_ > 0 && buffer->unparsed_received_bytes() > 0) {
    size_t unparsed_bytes = buffer->unparsed_received_bytes();
    const char * entry = buffer->unparsed_data();

    redis::Bulk bulk(entry, unparsed_bytes);
    if (bulk.present_size() < 0) {
      LOG_WARN << "ParseIncompleteQuery sub_bulk error";
      return false;
    }
    if (bulk.present_size() == 0) {
      LOG_WARN << "ParseIncompleteQuery sub_bulk need more data";
      return true;
    }
    LOG_WARN << "ParseIncompleteQuery parsed_bytes=" << bulk.total_size();
    buffer->update_parsed_bytes(bulk.total_size());
    --unparsed_bulks_;
  }
  return true;
}

bool RedisSetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  assert(backend_conn_ == backend);
  size_t unparsed = backend_conn_->buffer()->unparsed_bytes();
  assert(unparsed > 0);
  const char * entry = backend_conn_->buffer()->unparsed_data();
  if (entry[0] != '+' && entry[0] != '-' && entry[0] != '$') {
    LOG_DEBUG << "RedisSetCommand ParseReply unknown reply format(" << std::string(entry, unparsed) << ")";
    return false;
  }

  const char * p = GetLineEnd(entry, unparsed);
  if (p == nullptr) {
    LOG_DEBUG << "RedisSetCommand ParseReply no enough data for parsing, please read more"
              // << " data=" << std::string(entry, backend_conn_->buffer()->unparsed_bytes())
              << " bytes=" << backend_conn_->buffer()->unparsed_bytes();
    return true;
  }

  backend_conn_->buffer()->update_parsed_bytes(p - entry + 1);
  LOG_DEBUG << "RedisSetCommand ParseReply resp.size=" << p - entry + 1
            // << " contont=[" << std::string(entry, p - entry - 1) << "]"
            << " set_reply_recv_complete, backend=" << backend;
  backend->set_reply_recv_complete();
  return true;
}

}

