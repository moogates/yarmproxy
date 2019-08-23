#include "write_command.h"

#include "base/logging.h"
#include "client_conn.h"
#include "backend_conn.h"
#include "worker_pool.h"

namespace mcproxy {

const char * GetLineEnd(const char * buf, size_t len);

std::atomic_int write_cmd_count;
WriteCommand::WriteCommand(const ip::tcp::endpoint & ep, 
        std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len, size_t body_bytes)
    : MemcCommand(owner) 
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
    context_.backend_conn_pool_->Release(backend_conn_);
  }
  LOG_DEBUG << "WriteCommand dtor " << --write_cmd_count;
}

size_t WriteCommand::request_body_upcoming_bytes() const {
  return request_cmd_len_ + request_body_bytes_ - bytes_forwarding_ - request_forwarded_bytes_;
}

void WriteCommand::ForwardRequest(const char * data, size_t bytes) {
  if (backend_conn_ == nullptr) {
    // LOG_DEBUG << "MemcCommand(" << cmd_line_without_rn() << ") create backend conn, worker_id=" << WorkerPool::CurrentWorkerId();
    LOG_DEBUG << "MemcCommand(" << cmd_line_without_rn() << ") create backend conn";
    backend_conn_ = context_.backend_conn_pool_->Allocate(backend_endpoint_);
    backend_conn_->SetReadWriteCallback(WeakBind(&MemcCommand::OnForwardMoreRequest),
                               WeakBind2(&MemcCommand::OnUpstreamResponseReceived, backend_conn_));
  }

  DoForwardRequest(data, bytes);
}

void WriteCommand::DoForwardRequest(const char * request_data, size_t client_buf_received_bytes) {
  client_conn_->read_buffer_.inc_recycle_lock();
  bytes_forwarding_ = std::min(client_buf_received_bytes, request_cmd_len_ + request_body_bytes_); // FIXME
  backend_conn_->ForwardRequest(request_data, bytes_forwarding_, request_body_upcoming_bytes() != 0);
}

void WriteCommand::OnForwardMoreRequest(const boost::system::error_code& error) {
  if (error) {
    // TODO : error handling
    LOG_WARN << "WriteCommand OnForwardMoreRequest error";
    return;
  }
  LOG_INFO << "WriteCommand OnForwardMoreRequest ok, bytes_forwarding_=" << bytes_forwarding_;
  client_conn_->read_buffer_.dec_recycle_lock();

  if (bytes_forwarding_ < request_cmd_len_ + request_body_bytes_) {
    client_conn_->TryReadMoreRequest();
  } else {
    // TODO ?
    backend_conn_->ReadResponse();
  }
  request_forwarded_bytes_ += bytes_forwarding_;
  bytes_forwarding_ = 0;
}

bool WriteCommand::ParseUpstreamResponse(BackendConn* backend) {
  assert(backend_conn_ == backend);
  const char * entry = backend_conn_->read_buffer_.unparsed_data();
  const char * p = GetLineEnd(entry, backend_conn_->read_buffer_.unparsed_bytes());
  if (p == nullptr) {
    // TODO : no enough data for parsing, please read more
    LOG_DEBUG << "WriteCommand ParseUpstreamResponse no enough data for parsing, please read more"
              << " data=" << std::string(entry, backend_conn_->read_buffer_.unparsed_bytes())
              << " bytes=" << backend_conn_->read_buffer_.unparsed_bytes();
    return true;
  }

  backend_conn_->read_buffer_.update_parsed_bytes(p - entry + 1);
  LOG_WARN << "WriteCommand ParseUpstreamResponse resp=[" << std::string(entry, p - entry - 1) << "]";
  return true;
}

}

