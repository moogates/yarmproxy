#include "redis_basic_command.h"

#include "logging.h"

#include "key_locator.h"
#include "backend_pool.h"
#include "client_conn.h"

namespace yarmproxy {

RedisBasicCommand::RedisBasicCommand(std::shared_ptr<ClientConnection> client,
                                     const redis::BulkArray& ba)
    : Command(client, ProtocolType::REDIS) {
  auto ep = key_locator()->Locate(ba[1].payload_data(),
                ba[1].payload_size(), ProtocolType::REDIS);
  replying_backend_ = backend_pool()->Allocate(ep);
}

RedisBasicCommand::~RedisBasicCommand() {
  if (replying_backend_) {
    backend_pool()->Release(replying_backend_);
  }
}

}

