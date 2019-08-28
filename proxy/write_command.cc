#include "write_command.h"

#include "base/logging.h"

#include "backend_conn.h"
#include "client_conn.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace mcproxy {

const char * GetLineEnd(const char * buf, size_t len);

std::atomic_int write_cmd_count;
WriteCommand::WriteCommand(const ip::tcp::endpoint & ep, 
        std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len, size_t body_bytes)
    : Command(owner) 
    , request_cmd_line_(buf)
    , request_cmd_len_(cmd_len)
    , request_forwarded_bytes_(0)
    , request_body_bytes_(body_bytes)
    , bytes_forwarding_(0)
    , backend_endpoint_(ep)
    , backend_conn_(nullptr)
{
  LOG_DEBUG << "WriteCommand ctor " << ++write_cmd_count;
}

WriteCommand::~WriteCommand() {
  if (backend_conn_) {
    context_.backend_conn_pool()->Release(backend_conn_);
  }
  LOG_DEBUG << "WriteCommand dtor " << --write_cmd_count;
}

size_t WriteCommand::request_body_upcoming_bytes() const {
  return request_cmd_len_ + request_body_bytes_ - bytes_forwarding_ - request_forwarded_bytes_;
}

void WriteCommand::ForwardQuery(const char * data, size_t bytes) {
  if (backend_conn_ == nullptr) {
    backend_conn_ = context_.backend_conn_pool()->Allocate(backend_endpoint_);
    backend_conn_->SetReadWriteCallback(WeakBind(&Command::OnForwardQueryFinished, backend_conn_),
                               WeakBind(&Command::OnUpstreamReplyReceived, backend_conn_));
    LOG_DEBUG << "WriteCommand::ForwardQuery allocated backend=" << backend_conn_;
  }

  DoForwardQuery(data, bytes);
}

void WriteCommand::DoForwardQuery(const char * request_data, size_t client_buf_received_bytes) {
  client_conn_->buffer()->inc_recycle_lock();
  bytes_forwarding_ = std::min(client_buf_received_bytes, request_cmd_len_ + request_body_bytes_); // FIXME
  backend_conn_->ForwardQuery(request_data, bytes_forwarding_, request_body_upcoming_bytes() != 0);
}

void WriteCommand::OnForwardQueryFinished(BackendConn* backend, const boost::system::error_code& error) {
  if (error) {
    // TODO : error handling
    LOG_DEBUG << "WriteCommand OnForwardQueryFinished error";
    return;
  }
  assert(backend == backend_conn_);
  LOG_DEBUG << "WriteCommand OnForwardQueryFinished ok, bytes_forwarding_=" << bytes_forwarding_;
  client_conn_->buffer()->dec_recycle_lock();

  request_forwarded_bytes_ += bytes_forwarding_;
  bytes_forwarding_ = 0;

  if (request_forwarded_bytes_ < request_cmd_len_ + request_body_bytes_) {
    LOG_DEBUG << "WriteCommand::OnForwardQueryFinished 转发了当前所有可转发数据, 但还要转发更多来自client的数据.";
    client_conn_->TryReadMoreQuery();
  } else {
    LOG_DEBUG << "WriteCommand::OnForwardQueryFinished 转发了当前命令的所有数据, 等待 backend 的响应.";
    backend_conn_->ReadReply();
  }
}

bool WriteCommand::ParseReply(BackendConn* backend) {
  assert(backend_conn_ == backend);
  const char * entry = backend_conn_->buffer()->unparsed_data();
  const char * p = GetLineEnd(entry, backend_conn_->buffer()->unparsed_bytes());
  if (p == nullptr) {
    // TODO : no enough data for parsing, please read more
    LOG_DEBUG << "WriteCommand ParseReply no enough data for parsing, please read more"
              // << " data=" << std::string(entry, backend_conn_->buffer()->unparsed_bytes())
              << " bytes=" << backend_conn_->buffer()->unparsed_bytes();
    return true;
  }

  backend_conn_->buffer()->update_parsed_bytes(p - entry + 1);
  LOG_DEBUG << "WriteCommand ParseReply resp.size=" << p - entry + 1
            // << " contont=[" << std::string(entry, p - entry - 1) << "]"
            << " set_reply_complete, backend=" << backend;
  backend->set_reply_complete();
  return true;
}

}

