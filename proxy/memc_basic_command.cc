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

  backend_endpoint_ = backend_locator()->Locate(p, q - p,
                          ProtocolType::MEMCACHED);
  return 0; // 2 is length of the ending "\r\n"
}

MemcBasicCommand::~MemcBasicCommand() {
  if (replying_backend_) {
    backend_pool()->Release(replying_backend_);
  }
}

bool MemcBasicCommand::WriteQuery() {
  assert(replying_backend_ == nullptr);
  replying_backend_ = AllocateBackend(backend_endpoint_);
  LOG_DEBUG << "MemcBasicCommand::WriteQuery backend=" << replying_backend_;

  auto buffer = client_conn_->buffer();
  buffer->inc_recycle_lock();
  replying_backend_->WriteQuery(buffer->unprocessed_data(),
                            buffer->unprocessed_bytes());
  return false;
}

void MemcBasicCommand::StartWriteReply() {
  // TODO : report error & rotate if connection refused
  TryWriteReply(replying_backend_);
}

void MemcBasicCommand::RotateReplyingBackend(bool) {
  client_conn_->RotateReplyingCommand();
}

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

