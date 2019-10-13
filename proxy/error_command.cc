#include "error_command.h"

#include "logging.h"

#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

ErrorCommand::ErrorCommand(std::shared_ptr<ClientConnection> client,
                           const std::string& reply_message) 
    : Command(client, ProtocolType::NONE)
    , reply_message_(reply_message) {
}

ErrorCommand::~ErrorCommand() {
}

bool ErrorCommand::StartWriteQuery() {
  if (client_conn_->IsFirstCommand(shared_from_this())) {
    StartWriteReply();
  }
  return false;
}

void ErrorCommand::StartWriteReply() {
  // TODO : report error & rotate if connection refused
  client_conn_->WriteReply(reply_message_.data(), reply_message_.size(),
          WeakBind(&Command::OnWriteReplyFinished, nullptr));
}

void ErrorCommand::OnWriteReplyFinished(std::shared_ptr<BackendConn> backend,
                                   ErrorCode ec) {
  assert(backend == nullptr);
  LOG_DEBUG << "ErrorCommand OnWriteReplyFinished, backend=" << backend
            << " ec=" << ErrorCodeString(ec);
  client_conn_->Abort();
}

}

