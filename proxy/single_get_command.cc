#include "single_get_command.h"

#include "error_code.h"
#include "logging.h"

#include "backend_conn.h"
#include "client_conn.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

std::atomic_int single_get_cmd_count;

const char * GetLineEnd(const char * buf, size_t len);

size_t GetValueBytes(const char * data, const char * end) {
  // "VALUE <key> <flag> <bytes>\r\n"
  const char * p = data + sizeof("VALUE ");
  int count = 0;
  while(p != end) {
    if (*p == ' ') {
      if (++count == 2) {
        return std::stoi(p + 1);
      }
    }
    ++p;
  }
  return 0;
}


SingleGetCommand::SingleGetCommand(const ip::tcp::endpoint & ep,
        std::shared_ptr<ClientConnection> client, const char * buf, size_t cmd_len)
    : Command(client)
    , cmd_line_(buf, cmd_len)
    , backend_endpoint_(ep)
    , backend_conn_(nullptr)
{
  LOG_DEBUG << "SingleGetCommand ctor " << ++single_get_cmd_count;
}

SingleGetCommand::~SingleGetCommand() {
  if (backend_conn_) {
    context().backend_conn_pool()->Release(backend_conn_);
  }
  LOG_DEBUG << "SingleGetCommand dtor " << --single_get_cmd_count;
}

void SingleGetCommand::ForwardQuery(const char * data, size_t bytes) {
  if (backend_conn_ == nullptr) {
    backend_conn_ = context().backend_conn_pool()->Allocate(backend_endpoint_);
    backend_conn_->SetReadWriteCallback(WeakBind(&Command::OnForwardQueryFinished, backend_conn_),
                               WeakBind(&Command::OnUpstreamReplyReceived, backend_conn_));
    LOG_DEBUG << "SingleGetCommand::ForwardQuery allocated backend=" << backend_conn_;
  }

  DoForwardQuery(data, bytes);
}

// TODO : rename forward -> write
void SingleGetCommand::OnForwardQueryFinished(BackendConn* backend, ErrorCode ec) {
  // OnForwardQueryFinished(backend, boost::system::error_code());
  if (ec != ErrorCode::E_SUCCESS) {
    // TODO : error handling
    if (ec == ErrorCode::E_CONNECT) {
      LOG_WARN << "SingleGetCommand OnForwardQueryFinished connection_refused, endpoint=" << backend->remote_endpoint()
               << " backend=" << backend;
      backend->Close();
      // context().backend_conn_pool()->Release(backend);

      static const char BACKEND_ERROR[] = "BACKEND_CONNECTION_REFUSED\r\n"; // TODO : 统一放置错误码
      client_conn_->ErrorReport(BACKEND_ERROR, sizeof(BACKEND_ERROR) - 1);
      client_conn_->RotateReplyingCommand();
    } else {
      client_conn_->ErrorAbort();
      LOG_INFO << "SingleGetCommand OnForwardQueryFinished error";
    }
    return;
  }
  assert(backend == backend_conn_);
  LOG_DEBUG << "SingleGetCommand::OnForwardQueryFinished 转发了当前命令, 等待backend的响应.";
  backend_conn_->ReadReply();
}

bool SingleGetCommand::ParseReply(BackendConn* backend) {
  bool valid = true;
  assert(backend_conn_ == backend);
  while(backend_conn_->buffer()->unparsed_bytes() > 0) {
    const char * entry = backend_conn_->buffer()->unparsed_data();
    size_t unparsed_bytes = backend->buffer()->unparsed_bytes();
    const char * p = GetLineEnd(entry, unparsed_bytes);
    if (p == nullptr) {
      LOG_DEBUG << "ParseReply no enough data for parsing, please read more"
                << " data=" << std::string(entry, backend_conn_->buffer()->unparsed_bytes())
                << " bytes=" << backend_conn_->buffer()->unparsed_bytes();
      return true;
    }

    if (entry[0] == 'V') {
      // "VALUE <key> <flag> <bytes>\r\n"
      size_t body_bytes = GetValueBytes(entry, p);
      size_t entry_bytes = p - entry + 1 + body_bytes + 2;

      backend_conn_->buffer()->update_parsed_bytes(entry_bytes);
      LOG_DEBUG << __func__ << " VALUE data, backend=" << backend << " recv_body=(" << std::string(entry, std::min(unparsed_bytes, entry_bytes)) << ")";
      // break; // TODO : 每次转发一条，only for test
    } else {
      // "END\r\n"
      if (strncmp("END\r\n", entry, sizeof("END\r\n") - 1) == 0) {
        backend_conn_->buffer()->update_parsed_bytes(sizeof("END\r\n") - 1);
        // if (backend->buffer()->unparsed_bytes() != (sizeof("END\r\n") - 1)) { // TODO : pipeline的情况呢?
        if (backend->buffer()->unparsed_bytes() != 0) { // TODO : pipeline的情况呢?
          valid = false;
          LOG_DEBUG << "ParseReply END not really end!";
        } else {
          LOG_DEBUG << "ParseReply END is really end! set_reply_complete, backend=" << backend;
          backend->set_reply_complete();
          // backend->buffer()->cut_received_tail(sizeof("END\r\n") - 1);
        }
        break;
      } else {
        LOG_WARN << "ParseReply BAD DATA";
        // TODO : ERROR
        valid = false;
        break;
      }
    }
  }
  return valid;
}

void SingleGetCommand::DoForwardQuery(const char *, size_t) {
  backend_conn_->ForwardQuery(cmd_line_.data(), cmd_line_.size(), false);
}

}


