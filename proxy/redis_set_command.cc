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

RedisSetCommand::RedisSetCommand(std::shared_ptr<ClientConnection> client,
                                 const redis::BulkArray& ba)
    : Command(client, ProtocolType::REDIS)
    , unparsed_bulks_(ba.absent_bulks()) {
  backend_endpoint_ = backend_locator()->Locate(ba[1].payload_data(),
                          ba[1].payload_size(), ProtocolType::REDIS);
  LOG_DEBUG << "RedisSetCommand key=" << ba[1].to_string()
            << " ep=" << backend_endpoint_;
}

RedisSetCommand::~RedisSetCommand() {
  if (backend_conn_) {
    backend_pool()->Release(backend_conn_);
  }
}

bool RedisSetCommand::ContinueWriteQuery() {
  return WriteQuery();
}

bool RedisSetCommand::WriteQuery() {
  if (client_conn_->buffer()->parsed_unreceived_bytes() == 0 &&
      unparsed_bulks_ == 0) {
    query_recv_complete_ = true;
  }

  if (backend_conn_ && backend_conn_->error()) {
    assert(backend_conn_);
    if (!query_recv_complete_) {
      return true; // no callback, try read more query directly
    }
    if (client_conn_->IsFirstCommand(shared_from_this())) {
      // write reply
      TryWriteReply(backend_conn_);
    } else {
      // wait to write reply
    }
    return false;
  }

  if (!backend_conn_) {
    backend_conn_ = AllocateBackend(backend_endpoint_);
  }

  LOG_DEBUG << "RedisSetCommand " << this << " WriteQuery data, backend=" << backend_conn_
           << " ep=" << backend_conn_->remote_endpoint()
           << " client_buff=" << client_conn_->buffer()
           << " PRE-client_buff_lock_count=" << client_conn_->buffer()->recycle_lock_count()
           << " bytes=" << client_conn_->buffer()->unprocessed_bytes();
  client_conn_->buffer()->inc_recycle_lock();
  backend_conn_->WriteQuery(client_conn_->buffer()->unprocessed_data(),
          client_conn_->buffer()->unprocessed_bytes());
  return false;
}

void RedisSetCommand::StartWriteReply() {
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
    LOG_DEBUG << "ParseUnparsedPart parsed_bytes=" << bulk.total_size();
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

  LOG_DEBUG << "RedisSetCommand ParseReply begin, unparsed=" << unparsed
            << " data=[" << std::string(entry, unparsed)
            << "] backend=" << backend;
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
  if (entry[0] == '$' && entry[1] != '-') {
    p = static_cast<const char *>(memchr(p + 1, '\n', entry + unparsed - p));
    if (p == nullptr) {
      return true;
    }
  }

  backend_conn_->buffer()->update_parsed_bytes(p - entry + 1);
  LOG_DEBUG << "RedisSetCommand ParseReply complete, resp.size="
            << p - entry + 1 << " backend=" << backend;
  backend->set_reply_recv_complete();
  return true;
}

}

