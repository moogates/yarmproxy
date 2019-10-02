#include "redis_set_command.h"

#include "base/logging.h"

#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "worker_pool.h"

#include "redis_protocol.h"

namespace yarmproxy {

std::atomic_int redis_set_cmd_count;

RedisSetCommand::RedisSetCommand(std::shared_ptr<ClientConnection> client, const redis::BulkArray& ba)
    : Command(client)
    , unparsed_bulks_(ba.absent_bulks())
{
  backend_endpoint_ = BackendLoactor::Instance().Locate(ba[1].payload_data(),
                          ba[1].payload_size(), ProtocolType::REDIS);
  LOG_DEBUG << "RedisSetCommand key=" << ba[1].to_string() << " ep=" << backend_endpoint_
            << " ctor " << ++redis_set_cmd_count;
}

RedisSetCommand::~RedisSetCommand() {
  if (backend_conn_) {
    backend_pool()->Release(backend_conn_);
  }
  LOG_DEBUG << "RedisSetCommand dtor " << --redis_set_cmd_count;
}

bool RedisSetCommand::WriteQuery() {
  if (client_conn_->buffer()->parsed_unreceived_bytes() == 0 &&
      unparsed_bulks_ == 0) {
    LOG_DEBUG << "RedisSetCommand WriteQuery query_recv_complete_ true";
    query_recv_complete_ = true;
  } else {
    LOG_DEBUG << "RedisSetCommand WriteQuery query_recv_complete_ false";
  }

  if (connect_error_) {
    assert(backend_conn_);
    // LOG_WARN << "RedisSetCommand WriteQuery connect_error_ is true, query_recv_complete_=" << query_recv_complete_;
    if (query_recv_complete_) {
      if (client_conn_->IsFirstCommand(shared_from_this())) {
        LOG_WARN << "RedisSetCommand WriteQuery connect_error_ write reply";
        TryWriteReply(backend_conn_);
      } else {
        LOG_WARN << "RedisSetCommand WriteQuery connect_error_ wait to write reply";
      }
    } else {
      return true; // no callback, try read more query directly
    }
    return false;
  }

  if (!backend_conn_) {
    backend_conn_ = AllocateBackend(backend_endpoint_);
    // LOG_WARN << "RedisSetCommand WriteQuery begin, backend=" << backend_conn_;
  }

  LOG_DEBUG << "RedisSetCommand WriteQuery data, backend=" << backend_conn_
           << " bytes=" << client_conn_->buffer()->unprocessed_bytes();
  client_conn_->buffer()->inc_recycle_lock();
  backend_conn_->WriteQuery(client_conn_->buffer()->unprocessed_data(),
          client_conn_->buffer()->unprocessed_bytes());
  return false;
}

void RedisSetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS || !ParseReply(backend)) {
    LOG_WARN << "Command::OnBackendReplyReceived error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  if (client_conn_->IsFirstCommand(shared_from_this())) {
    LOG_DEBUG << "OnBackendReplyReceived TryWriteReply backend=" << backend;
    TryWriteReply(backend);
  } else {
    LOG_DEBUG << "OnBackendReplyReceived wait to WriteReply backend=" << backend;
  }
  backend->TryReadMoreReply();
}

void RedisSetCommand::StartWriteReply() {
  LOG_DEBUG << "StartWriteReply TryWriteReply backend_conn_=" << backend_conn_;
  if (query_recv_complete_) {
    TryWriteReply(backend_conn_);
  }
}

void RedisSetCommand::RotateReplyingBackend(bool) {
  assert(query_recv_complete_);
  client_conn_->RotateReplyingCommand();
}

bool RedisSetCommand::query_parsing_complete() {
  return unparsed_bulks_ == 0;
}

void RedisSetCommand::OnBackendConnectError(std::shared_ptr<BackendConn> backend) {
  static const char BACKEND_ERROR[] = "-BACKEND_CONNECT_ERROR\r\n"; // TODO :refining error message
  backend->SetReplyData(BACKEND_ERROR, sizeof(BACKEND_ERROR) - 1);
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  connect_error_ = true;

  if (query_recv_complete_) {
    if (client_conn_->IsFirstCommand(shared_from_this())) {
      LOG_WARN << "RedisSetCommand OnBackendConnectError write reply";
      TryWriteReply(backend);
    } else {
      LOG_WARN << "RedisSetCommand OnBackendConnectError waiting to write reply";
    }
  } else {
    LOG_WARN << "RedisSetCommand OnBackendConnectError waiting for more query data";
  }
}

bool RedisSetCommand::ParseUnparsedPart() {
  ReadBuffer* buffer = client_conn_->buffer();
  while(unparsed_bulks_ > 0 && buffer->unparsed_received_bytes() > 0) {
    size_t unparsed_bytes = buffer->unparsed_received_bytes();
    const char * entry = buffer->unparsed_data();

    redis::Bulk bulk(entry, unparsed_bytes);
    if (bulk.present_size() < 0) {
      return false;
    }
    if (bulk.present_size() == 0) {
      break;
    }
    LOG_WARN << "ParseUnparsedPart parsed_bytes=" << bulk.total_size();
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
  if (entry[0] != ':' && entry[0] != '+' && entry[0] != '-' &&
      entry[0] != '$') { // TODO : fix the $ reply (aka. bulk)
    LOG_DEBUG << "RedisSetCommand ParseReply unknown format["
              << std::string(entry, unparsed) << "]";
    return false;
  }

  const char * p = static_cast<const char *>(memchr(entry, '\n', unparsed));
  if (p == nullptr) {
    return true;
  }
  if (entry[0] == '$') {
    p = static_cast<const char *>(memchr(p + 1, '\n', entry + unparsed - p));
    if (p == nullptr) {
      return true;
    }
  }

  backend_conn_->buffer()->update_parsed_bytes(p - entry + 1);
  LOG_DEBUG << "RedisSetCommand ParseReply complete, resp.size=" << p - entry + 1
            << " backend=" << backend;
  backend->set_reply_recv_complete();
  return true;
}

}

