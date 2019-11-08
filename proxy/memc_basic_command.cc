#include "memc_basic_command.h"

#include "logging.h"

#include "key_locator.h"
#include "backend_pool.h"

namespace yarmproxy {

MemcBasicCommand::MemcBasicCommand(
    std::shared_ptr<ClientConnection> client, const char* buf)
    : Command(client, ProtocolType::MEMCACHED) {
  const char *p = buf;
  while(*(p++) != ' ');

  const char *q = p;
  while(*(++q) != ' ' && *q != '\r');

  auto ep = key_locator()->Locate(p, q - p, ProtocolType::MEMCACHED);
  replying_backend_ = backend_pool()->Allocate(ep);
}

MemcBasicCommand::~MemcBasicCommand() {
  if (replying_backend_) {
    backend_pool()->Release(replying_backend_);
  }
}

}

