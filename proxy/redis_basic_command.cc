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

