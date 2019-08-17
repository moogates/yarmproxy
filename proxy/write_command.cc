#include "write_command.h"

#include "base/logging.h"
#include "client_conn.h"
#include "upstream_conn.h"

namespace mcproxy {

UpstreamReadCallback WrapOnUpstreamResponse(std::weak_ptr<MemcCommand> cmd_wptr);
UpstreamWriteCallback WrapOnUpstreamRequestWritten(std::weak_ptr<MemcCommand> cmd_wptr);

const char * GetLineEnd(const char * buf, size_t len);

std::atomic_int write_cmd_count;
WriteCommand::WriteCommand(boost::asio::io_service& io_service, const ip::tcp::endpoint & ep, 
        std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len, size_t body_bytes)
    : MemcCommand(io_service, ep, owner, buf, cmd_len) 
    // , is_forwarding_request_(false)
    // , is_forwarding_response_(false)
    , request_forwarded_bytes_(0)
    , request_body_bytes_(body_bytes)
    , bytes_forwarding_(0)
{
  LOG_DEBUG << "WriteCommand ctor " << ++write_cmd_count;
}

WriteCommand::~WriteCommand() {
  LOG_DEBUG << "WriteCommand dtor " << --write_cmd_count;
}

size_t WriteCommand::upcoming_bytes() const {
  return cmd_line_.size() + request_body_bytes_ - bytes_forwarding_ - request_forwarded_bytes_;
}

// TODO : merge ForwardRequest() into base class
void WriteCommand::ForwardRequest(const char * request_data, size_t client_buf_received_bytes) {
  if (upstream_conn_ == nullptr) {
    LOG_DEBUG << "WriteCommand(" << cmd_line_.substr(0, cmd_line_.size() - 2) << ") create upstream conn";
    upstream_conn_ = new UpstreamConn(io_service_, upstream_endpoint_, WrapOnUpstreamResponse(shared_from_this()),
                                      WrapOnUpstreamRequestWritten(shared_from_this()));
  } else {
    LOG_DEBUG << "WriteCommand(" << cmd_line_.substr(0, cmd_line_.size() - 2) << ") reuse upstream conn";
    upstream_conn_->set_upstream_read_callback(WrapOnUpstreamResponse(shared_from_this()),
                                               WrapOnUpstreamRequestWritten(shared_from_this()));
  }

  bytes_forwarding_ = std::min(client_buf_received_bytes, cmd_line_.size() + body_bytes_); // FIXME
  upstream_conn_->ForwardRequest(request_data, bytes_forwarding_, upcoming_bytes() != 0);
}

void WriteCommand::OnUpstreamRequestWritten(size_t, const boost::system::error_code& error) {
  if (error) {
    // TODO : error handling
    LOG_WARN << "WriteCommand OnUpstreamRequestWritten error";
    return;
  }
  LOG_INFO << "WriteCommand OnUpstreamRequestWritten ok, bytes_forwarding_=" << bytes_forwarding_;
  client_conn_->recursive_unlock_buffer();

  if (bytes_forwarding_ < cmd_line_.size() + body_bytes_) {
    client_conn_->TryReadMoreRequest();
  } else {
    // TODO ?
    upstream_conn_->ReadResponse();
  }
  request_forwarded_bytes_ += bytes_forwarding_;
  bytes_forwarding_ = 0;
}

bool WriteCommand::ParseUpstreamResponse() {
  const char * entry = upstream_conn_->unparsed_data();
  const char * p = GetLineEnd(entry, upstream_conn_->unparsed_bytes());
  if (p == nullptr) {
    // TODO : no enough data for parsing, please read more
    LOG_DEBUG << "WriteCommand ParseUpstreamResponse no enough data for parsing, please read more"
              << " data=" << std::string(entry, upstream_conn_->unparsed_bytes())
              << " bytes=" << upstream_conn_->unparsed_bytes();
    return true;
  }

  set_upstream_nomore_data();
  upstream_conn_->update_parsed_bytes(p - entry + 1);
  LOG_WARN << "WriteCommand ParseUpstreamResponse resp=[" << std::string(entry, p - entry - 1) << "]";
  return true;
}

}

