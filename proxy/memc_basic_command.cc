#include "memc_basic_command.h"

#include "logging.h"

#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

MemcBasicCommand::MemcBasicCommand(
    std::shared_ptr<ClientConnection> client, const char* buf, size_t cmd_len) 
    : Command(client, ProtocolType::MEMCACHED) {
  ParseQuery(buf, cmd_len);
}

size_t MemcBasicCommand::ParseQuery(const char* cmd_data, size_t cmd_len) {
  // <command> <key> [noreply]\r\n
  const char *p = cmd_data;
  while(*(p++) != ' ');

  const char *q = p;
  while(*(++q) != ' ' && *q != '\r');

  auto ep = backend_locator()->Locate(p, q - p, ProtocolType::MEMCACHED);
  replying_backend_ = backend_pool()->Allocate(ep);
  return 0; // 2 is length of the ending "\r\n"
}

MemcBasicCommand::~MemcBasicCommand() {
  if (replying_backend_) {
    backend_pool()->Release(replying_backend_);
  }
}

/*
bool MemcBasicCommand::StartWriteQuery() {
  LOG_DEBUG << "MemcBasicCommand::StartWriteQuery backend=" << replying_backend_;
  return Command::StartWriteQuery();

  assert(replying_backend_);
  check_query_recv_complete();

  assert(first_write_query_);
  replying_backend_->SetReadWriteCallback(
      WeakBind(&Command::OnWriteQueryFinished, replying_backend_),
      WeakBind(&Command::OnBackendReplyReceived, replying_backend_));
  first_write_query_ = false; // TODO : remove this var
  assert(replying_backend_ == nullptr);

  client_conn_->buffer()->inc_recycle_lock();
  replying_backend_->WriteQuery(client_conn_->buffer()->unprocessed_data(),
                            client_conn_->buffer()->unprocessed_bytes());
  return false;
}
*/

bool MemcBasicCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  assert(replying_backend_ == backend);
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

