#include "redis_get_command.h"

#include "logging.h"

#include "error_code.h"
#include "backend_conn.h"
#include "backend_pool.h"
#include "backend_locator.h"
#include "client_conn.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

RedisGetCommand::RedisGetCommand(std::shared_ptr<ClientConnection> client,
                                 const redis::BulkArray& ba)
    : Command(client, ProtocolType::REDIS)
    , cmd_data_(ba.raw_data()) // TODO : remove cmd_data_ & cmd_bytes_
    , cmd_bytes_(ba.total_size())
{
  auto ep = backend_locator()->Locate(ba[1].payload_data(),
                 ba[1].payload_size(), ProtocolType::REDIS);
  LOG_DEBUG << "RedisSetCommand key=" << ba[1].to_string()
            << " ep=" << ep;
  replying_backend_ = backend_pool()->Allocate(ep);
}

RedisGetCommand::~RedisGetCommand() {
  if (replying_backend_) {
    backend_pool()->Release(replying_backend_);
  }
}

void RedisGetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
        ErrorCode ec) {
  assert(backend == replying_backend_);
  if (ec == ErrorCode::E_SUCCESS && !ParseReply(backend)) {
    ec = ErrorCode::E_PROTOCOL;
  }
  if (ec != ErrorCode::E_SUCCESS) {
    if (!BackendErrorRecoverable(backend, ec)) {
      client_conn_->Abort();
    } else {
      OnBackendRecoverableError(backend, ec);
    }
    return;
  }

  if (client_conn_->IsFirstCommand(shared_from_this())) {
    // write reply
    TryWriteReply(backend);
  } else {
    // wait to write reply
  }
  backend->TryReadMoreReply();
}

bool RedisGetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  size_t unparsed_bytes = backend->buffer()->unparsed_bytes();

  if (unparsed_bytes == 0) {
    if (backend->buffer()->parsed_unreceived_bytes() == 0) {
      backend->set_reply_recv_complete();
    }
    return true;
  }

  const char * entry = backend->buffer()->unparsed_data();
  redis::Bulk bulk(entry, unparsed_bytes);
  if (bulk.present_size() < 0) {
    return false;
  }
  if (bulk.present_size() == 0) {
    return true;
  }

  if (bulk.completed()) {
    LOG_DEBUG << "ParseReply bulk completed";
    backend->set_reply_recv_complete();
  } else {
    LOG_DEBUG << "ParseReply bulk not completed";
  }
  LOG_DEBUG << "ParseReply parsed_bytes=" << bulk.total_size();
  backend->buffer()->update_parsed_bytes(bulk.total_size());
  return true;
}

}

