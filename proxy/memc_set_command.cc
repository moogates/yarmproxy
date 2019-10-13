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
          const char* buf, size_t cmd_len, size_t* body_bytes) 
    : Command(client, ProtocolType::MEMCACHED) {
  *body_bytes = ParseQuery(buf, cmd_len);
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
/*
void MemcSetCommand::OnBackendRecoverableError(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  auto& err_reply(MemcErrorReply(ec));
  backend->SetReplyData(err_reply.data(), err_reply.size());
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  if (query_recv_complete()) {
    if (client_conn_->IsFirstCommand(shared_from_this())) {
      // write reply
      TryWriteReply(backend);
    } else {
      // waiting to write reply
    }
  } else {
    // wait for more query data
  }
}
*/

bool MemcSetCommand::query_recv_complete() {
  return query_recv_complete_;
}

bool MemcSetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  assert(replying_backend_== backend);
  const char * entry = replying_backend_->buffer()->unparsed_data();
  const char * p = static_cast<const char *>(memchr(entry, '\n',
                       replying_backend_->buffer()->unparsed_bytes()));
  if (p == nullptr) {
    return true;
  }

  replying_backend_->buffer()->update_parsed_bytes(p - entry + 1);
  backend->set_reply_recv_complete();
  return true;
}

}

