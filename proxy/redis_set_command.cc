#include "redis_set_command.h"

#include "logging.h"
#include "key_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "read_buffer.h"
#include "redis_protocol.h"

namespace yarmproxy {

RedisSetCommand::RedisSetCommand(std::shared_ptr<ClientConnection> client,
                                 const redis::BulkArray& ba)
    : Command(client, ProtocolType::REDIS)
    , unparsed_bulks_(ba.absent_bulks()) {
  auto ep = key_locator()->Locate(ba[1].payload_data(),
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

void RedisSetCommand::check_query_recv_complete() {
  if (client_conn_->buffer()->parsed_unreceived_bytes() == 0 &&
      unparsed_bulks_ == 0) {
    query_recv_complete_ = true;
  }
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

}

