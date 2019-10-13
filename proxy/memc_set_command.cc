#include "memc_set_command.h"

#include "logging.h"

#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

MemcSetCommand::MemcSetCommand(std::shared_ptr<ClientConnection> client,
          const char* cmd_data, size_t cmd_len, size_t* body_bytes) 
    : Command(client, ProtocolType::MEMCACHED) {
  *body_bytes = ParseQuery(cmd_data, cmd_len);
}

size_t MemcSetCommand::ParseQuery(const char* cmd_data, size_t cmd_len) {
  // TODO : strict check
  // <command name> <key> <flags> <exptime> <bytes>\r\n
  const char *p = cmd_data;
  while(*(p++) != ' ');

  const char *q = p;
  while(*(++q) != ' ');

  auto ep = backend_locator()->Locate(p, q - p, ProtocolType::MEMCACHED);
  replying_backend_ = backend_pool()->Allocate(ep);

  p = cmd_data + cmd_len - 2;
  while(*(p - 1) != ' ') {
    --p;
  }
  try {
    return std::atoi(p) + 2; // 2 is length of the ending "\r\n"
  } catch(...) {
    return 0;
  }
}

MemcSetCommand::~MemcSetCommand() {
  if (replying_backend_) {
    backend_pool()->Release(replying_backend_);
  }
}

void MemcSetCommand::check_query_recv_complete() {
  if (client_conn_->buffer()->parsed_unreceived_bytes() == 0) {
    query_recv_complete_ = true;
  }
}

bool MemcSetCommand::query_recv_complete() {
  return query_recv_complete_;
}

}

