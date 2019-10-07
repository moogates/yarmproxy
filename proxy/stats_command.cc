#include "stats_command.h"

#include "logging.h"

#include "stats.h"
#include "backend_conn.h"
#include "backend_locator.h"
#include "client_conn.h"
#include "error_code.h"
// #include "read_buffer.h"
// #include "worker_pool.h"

namespace yarmproxy {

Stats g_stats_;

StatsCommand::StatsCommand(std::shared_ptr<ClientConnection> client,
                           ProtocolType protocol)
    : Command(client)
    , protocol_(protocol) {
  if (protocol_ == ProtocolType::REDIS) {
    reply_message_ = "+";
  }
  reply_message_.reserve(256);
  reply_message_.append("start_since ")
      .append(std::to_string(g_stats_.start_since_))
      .append("\tclient_conns ")
      .append(std::to_string(g_stats_.client_conns_))
      .append("\tbackend_conns ")
      .append(std::to_string(g_stats_.backend_conns_))
      .append("\tbytes_from_clients ")
      .append(std::to_string(g_stats_.bytes_from_clients_))
      .append("\tbytes_to_clients ")
      .append(std::to_string(g_stats_.bytes_to_clients_))
      .append("\tbytes_from_backends ")
      .append(std::to_string(g_stats_.bytes_from_backends_))
      .append("\tbytes_to_backends ")
      .append(std::to_string(g_stats_.bytes_to_backends_));
}

StatsCommand::~StatsCommand() {
}

bool StatsCommand::WriteQuery() {
  if (client_conn_->IsFirstCommand(shared_from_this())) {
    StartWriteReply();
  }
  return false;
}

void StatsCommand::StartWriteReply() {
  // TODO : report error & rotate if connection refused
  client_conn_->WriteReply(reply_message_.data(), reply_message_.size(),
          WeakBind(&Command::OnWriteReplyFinished, nullptr));
}

void StatsCommand::OnWriteReplyFinished(std::shared_ptr<BackendConn> backend,
                                   ErrorCode ec) {
  assert(backend == nullptr);
  LOG_DEBUG << "StatsCommand OnWriteReplyFinished, backend=" << backend << " ec=" << int(ec);
  client_conn_->Abort();
}


void StatsCommand::RotateReplyingBackend(bool) {
  client_conn_->RotateReplyingCommand();
}

}

