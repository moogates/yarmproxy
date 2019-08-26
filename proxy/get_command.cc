#include "get_command.h"

#include "base/logging.h"
#include "client_conn.h"
#include "backend_conn.h"
#include "worker_pool.h"

namespace mcproxy {

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
        std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len)
    : MemcCommand(owner) 
    , cmd_line_(buf, cmd_len)
    , backend_endpoint_(ep)
    , backend_conn_(nullptr)
{
  LOG_DEBUG << "SingleGetCommand ctor " << ++single_get_cmd_count;
}

SingleGetCommand::~SingleGetCommand() {
  if (backend_conn_) {
    context_.backend_conn_pool()->Release(backend_conn_);
  }
  LOG_DEBUG << "SingleGetCommand dtor " << --single_get_cmd_count;
}

void SingleGetCommand::ForwardQuery(const char * data, size_t bytes) {
  if (backend_conn_ == nullptr) {
    // LOG_DEBUG << "MemcCommand(" << cmd_line_without_rn() << ") create backend conn, worker_id=" << WorkerPool::CurrentWorkerId();
    LOG_DEBUG << "MemcCommand(" << cmd_line_without_rn() << ") create backend conn";
    backend_conn_ = context_.backend_conn_pool()->Allocate(backend_endpoint_);
    backend_conn_->SetReadWriteCallback(WeakBind(&MemcCommand::OnForwardQueryFinished, backend_conn_),
                               WeakBind(&MemcCommand::OnUpstreamResponseReceived, backend_conn_));
  }

  DoForwardQuery(data, bytes);
}

void SingleGetCommand::OnForwardQueryFinished(BackendConn* backend, const boost::system::error_code& error) {
  if (error) {
    // TODO : error handling
    LOG_INFO << "WriteCommand OnForwardQueryFinished error";
    return;
  }
  assert(backend == backend_conn_);
  LOG_DEBUG << "SingleGetCommand::OnForwardQueryFinished 转发了当前命令, 等待backend的响应.";
  backend_conn_->ReadResponse();
}

bool SingleGetCommand::ParseUpstreamResponse(BackendConn* backend) {
  bool valid = true;
  assert(backend_conn_ == backend);
  while(backend_conn_->buffer()->unparsed_bytes() > 0) {
    const char * entry = backend_conn_->buffer()->unparsed_data();
    const char * p = GetLineEnd(entry, backend_conn_->buffer()->unparsed_bytes());
    if (p == nullptr) {
      // TODO : no enough data for parsing, please read more
      LOG_DEBUG << "ParseUpstreamResponse no enough data for parsing, please read more"
                << " data=" << std::string(entry, backend_conn_->buffer()->unparsed_bytes())
                << " bytes=" << backend_conn_->buffer()->unparsed_bytes();
      return true;
    }

    if (entry[0] == 'V') {
      // "VALUE <key> <flag> <bytes>\r\n"
      size_t body_bytes = GetValueBytes(entry, p);
      size_t entry_bytes = p - entry + 1 + body_bytes + 2;

      backend_conn_->buffer()->update_parsed_bytes(entry_bytes);
      // break; // TODO : 每次转发一条，only for test
    } else {
      // "END\r\n"
      if (strncmp("END\r\n", entry, sizeof("END\r\n") - 1) == 0) {
        backend_conn_->buffer()->update_parsed_bytes(sizeof("END\r\n") - 1);
        if (backend_conn_->buffer()->unparsed_bytes() != 0) { // TODO : pipeline的情况呢?
          valid = false;
          LOG_DEBUG << "ParseUpstreamResponse END not really end!";
        } else {
          LOG_DEBUG << "ParseUpstreamResponse END is really end!";
        }
        break;
      } else {
        LOG_WARN << "ParseUpstreamResponse BAD DATA";
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


