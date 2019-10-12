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
  auto ep = backend_locator()->Locate(ba[1].payload_data(),
                 ba[1].payload_size(), ProtocolType::REDIS);
  LOG_DEBUG << "RedisSetCommand key=" << ba[1].to_string()
            << " ep=" << ep;
  replying_backend_ = backend_pool()->Allocate(ep);
}

RedisSetCommand::~RedisSetCommand() {
  if (replying_backend_) {
    backend_pool()->Release(replying_backend_);
  }
}

bool RedisSetCommand::ContinueWriteQuery() {
  return WriteQuery();
}

void RedisSetCommand::update_check_query_recv_complete() {
  if (client_conn_->buffer()->parsed_unreceived_bytes() == 0 &&
      unparsed_bulks_ == 0) {
    query_recv_complete_ = true;
  }
}
/*
bool RedisSetCommand::WriteQuery() {
  if (first_write_query_) {
    replying_backend_->SetReadWriteCallback(
        WeakBind(&Command::OnWriteQueryFinished, replying_backend_),
        WeakBind(&Command::OnBackendReplyReceived, replying_backend_));
    first_write_query_ = false;
  }

  update_check_query_recv_complete();

  if (replying_backend_ && replying_backend_->error()) {
    if (!query_recv_complete()) {
      return true; // no callback, try read more query directly
    }
    if (client_conn_->IsFirstCommand(shared_from_this())) {
      // write reply
      TryWriteReply(replying_backend_);
    } else {
      // wait to write reply
    }
    return false;
  }

  assert(replying_backend_);
  LOG_DEBUG << "RedisSetCommand " << this << " WriteQuery data, backend=" << replying_backend_
           << " ep=" << replying_backend_->remote_endpoint()
           << " client_buff=" << client_conn_->buffer()
           << " PRE-client_buff_lock_count=" << client_conn_->buffer()->recycle_lock_count()
           << " bytes=" << client_conn_->buffer()->unprocessed_bytes();
  auto buffer = client_conn_->buffer();
  buffer->inc_recycle_lock();
  replying_backend_->WriteQuery(buffer->unprocessed_data(),
                            buffer->unprocessed_bytes());
  return false;
}
*/
void RedisSetCommand::StartWriteReply() {
  if (query_recv_complete_) {
    TryWriteReply(replying_backend_);
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
  assert(replying_backend_ == backend);
  size_t unparsed = replying_backend_->buffer()->unparsed_bytes();
  assert(unparsed > 0);
  const char * entry = replying_backend_->buffer()->unparsed_data();

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

  replying_backend_->buffer()->update_parsed_bytes(p - entry + 1);
  LOG_DEBUG << "RedisSetCommand ParseReply complete, resp.size="
            << p - entry + 1 << " backend=" << backend;
  backend->set_reply_recv_complete();
  return true;
}

}

