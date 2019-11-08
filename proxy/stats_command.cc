#include "stats_command.h"

#include "logging.h"

#include "stats.h"
#include "client_conn.h"
#include "error_code.h"

namespace yarmproxy {

Stats g_stats_;

StatsCommand::StatsCommand(std::shared_ptr<ClientConnection> client,
                           ProtocolType protocol)
    : Command(client, protocol) {
  if (protocol == ProtocolType::REDIS) {
    reply_message_ = "+";
  }
  reply_message_.reserve(256);
  reply_message_.append("alive_since=")
      .append(std::to_string(g_stats_.alive_since_))
      .append(",client_conns=")
      .append(std::to_string(g_stats_.client_conns_))
      .append(",backend_conns=")
      .append(std::to_string(g_stats_.backend_conns_))
      .append(",bytes_from_clients=")
      .append(std::to_string(g_stats_.bytes_from_clients_))
      .append(",bytes_to_clients=")
      .append(std::to_string(g_stats_.bytes_to_clients_))
      .append(",bytes_from_backends=")
      .append(std::to_string(g_stats_.bytes_from_backends_))
      .append(",bytes_to_backends=")
      .append(std::to_string(g_stats_.bytes_to_backends_))
      .append(",client_read_timeouts=")
      .append(std::to_string(g_stats_.client_read_timeouts_))
      .append(",client_write_timeouts=")
      .append(std::to_string(g_stats_.client_write_timeouts_))
      .append(",backend_connect_errors=")
      .append(std::to_string(g_stats_.backend_connect_errors_))
      .append(",backend_connect_timeouts=")
      .append(std::to_string(g_stats_.backend_connect_timeouts_))
      .append(",backend_read_timeouts=")
      .append(std::to_string(g_stats_.backend_read_timeouts_))
      .append(",backend_write_timeouts=")
      .append(std::to_string(g_stats_.backend_write_timeouts_))
      .append("\r\n");
}

StatsCommand::~StatsCommand() {
}

bool StatsCommand::StartWriteQuery() {
  if (client_conn_->IsFirstCommand(shared_from_this())) {
    StartWriteReply();
  }
  return false;
}

void StatsCommand::StartWriteReply() {
  client_conn_->WriteReply(reply_message_.data(), reply_message_.size(),
       WeakBind(&Command::OnWriteReplyFinished, nullptr));
}

void StatsCommand::OnWriteReplyFinished(std::shared_ptr<BackendConn> backend,
                                   ErrorCode ec) {
  assert(backend == nullptr);
  LOG_DEBUG << "StatsCommand OnWriteReplyFinished, backend=" << backend
            << " ec=" << ErrorCodeString(ec);
  RotateReplyingBackend();
}

}

