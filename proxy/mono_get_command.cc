#include "mono_get_command.h"

#include "backend_conn.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "logging.h"
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


MonoGetCommand::MonoGetCommand(const ip::tcp::endpoint & ep,
        std::shared_ptr<ClientConnection> client, const char * buf, size_t cmd_len)
    : Command(client, std::string(buf, cmd_len))
    , cmd_line_(buf, cmd_len)
    , backend_endpoint_(ep)
    , backend_conn_(nullptr)
{
  LOG_DEBUG << "MonoGetCommand ctor " << ++single_get_cmd_count;
}

MonoGetCommand::~MonoGetCommand() {
  if (backend_conn_) {
    context().backend_conn_pool()->Release(backend_conn_);
  }
  LOG_DEBUG << "MonoGetCommand dtor " << --single_get_cmd_count;
}

void MonoGetCommand::WriteQuery(const char * data, size_t bytes) {
  if (!backend_conn_) {
    backend_conn_ = context().backend_conn_pool()->Allocate(backend_endpoint_);
    backend_conn_->SetReadWriteCallback(WeakBind(&Command::OnWriteQueryFinished, backend_conn_),
                               WeakBind(&Command::OnUpstreamReplyReceived, backend_conn_));
    LOG_DEBUG << "MonoGetCommand::WriteQuery allocated backend=" << backend_conn_.get();
  }

  DoWriteQuery(data, bytes);
}

void MonoGetCommand::OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  // OnWriteQueryFinished(backend, boost::system::error_code());
  if (ec != ErrorCode::E_SUCCESS) {
    if (ec == ErrorCode::E_CONNECT) {
      LOG_WARN << "MonoGetCommand OnWriteQueryFinished connection_refused, endpoint=" << backend->remote_endpoint()
               << " backend=" << backend.get();
      OnBackendConnectError(backend);
    } else {
      client_conn_->Abort();
      LOG_INFO << "MonoGetCommand OnWriteQueryFinished error";
    }
    return;
  }
  assert(backend == backend_conn_);
  LOG_DEBUG << "MonoGetCommand::OnWriteQueryFinished 转发了当前命令, 等待backend的响应.";
  backend_conn_->ReadReply();
}

bool MonoGetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
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
      LOG_DEBUG << __func__ << " VALUE data, backend=" << backend.get()
                << " recv_body=(" << std::string(entry, std::min(unparsed_bytes, entry_bytes)) << ")";
      // break; // TODO : 每次转发一条，only for test
    } else {
      // "END\r\n"
      if (strncmp("END\r\n", entry, sizeof("END\r\n") - 1) == 0) {
        backend_conn_->buffer()->update_parsed_bytes(sizeof("END\r\n") - 1);
        if (backend->buffer()->unparsed_bytes() != 0) {
          valid = false;
          LOG_DEBUG << "ParseReply END not really end!";
        } else {
          LOG_DEBUG << "ParseReply END is really end! set_reply_complete, backend=" << backend.get();
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

void MonoGetCommand::DoWriteQuery(const char *, size_t) {
  backend_conn_->WriteQuery(cmd_line_.data(), cmd_line_.size(), false);
}

}


