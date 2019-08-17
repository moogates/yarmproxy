#include "write_command.h"

#include "base/logging.h"
#include "client_conn.h"
#include "upstream_conn.h"

namespace mcproxy {

UpstreamReadCallback WrapOnUpstreamResponse(std::weak_ptr<MemcCommand> cmd_wptr);
UpstreamWriteCallback WrapOnUpstreamRequestWritten(std::weak_ptr<MemcCommand> cmd_wptr);
ForwardResponseCallback WrapOnForwardResponseFinished(size_t to_transfer_bytes, std::weak_ptr<MemcCommand> cmd_wptr);

const char * GetLineEnd(const char * buf, size_t len);

std::atomic_int write_cmd_count;
WriteCommand::WriteCommand(boost::asio::io_service& io_service, const ip::tcp::endpoint & ep, 
        std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len, size_t body_bytes)
    : MemcCommand(io_service, ep, owner, buf, cmd_len) 
    , is_forwarding_request_(false)
    , is_forwarding_response_(false)
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

bool WriteCommand::ParseUpstreamWriteResponse() {
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

void WriteCommand::OnForwardResponseFinished(size_t bytes, const boost::system::error_code& error) {
  if (error) {
    // TODO
    LOG_DEBUG << "WriteCommand::OnForwardResponseFinished (" << cmd_line_.substr(0, cmd_line_.size() - 2) << ") error=" << error;
    return;
  }

  upstream_conn_->update_transfered_bytes(bytes);

  if (upstream_nomore_data() && upstream_conn_->to_transfer_bytes() == 0) {
    client_conn_->RotateFirstCommand();
    LOG_DEBUG << "WriteCommand::OnForwardResponseFinished upstream_nomore_data, and all data forwarded to client";
  } else {
    LOG_DEBUG << "WriteCommand::OnForwardResponseFinished upstream transfered_bytes=" << bytes
              << " ready_to_transfer_bytes=" << upstream_conn_->to_transfer_bytes();
    is_forwarding_response_ = false;
    if (!upstream_nomore_data()) {
      upstream_conn_->TryReadMoreData(); // 这里必须继续try
    }

    OnForwardResponseReady(); // 可能已经有新读到的数据，因而要尝试转发更多
  }
}

void WriteCommand::OnUpstreamResponse(const boost::system::error_code& error) {
  if (error) {
    // MCE_WARN(cmd_line_ << " upstream read error : " << upstream_endpoint_ << " - "  << error << " " << error.message());
    LOG_WARN << "WriteCommand OnUpstreamResponse error";
    client_conn_->OnCommandError(shared_from_this(), error);
    return;
  }
  LOG_DEBUG << "WriteCommand OnUpstreamResponse data";

  bool valid = ParseUpstreamWriteResponse();
  if (!valid) {
    LOG_WARN << "WriteCommand parsing error! valid=false";
    // TODO : error handling
  }
  if (IsFormostCommand()) {
    if (!is_forwarding_response_) {
      is_forwarding_response_ = true; // TODO : 这个flag是否真的需要? 需要，防止重复的写回请求
      auto cb_wrap = WrapOnForwardResponseFinished(upstream_conn_->to_transfer_bytes(), shared_from_this());
      client_conn_->ForwardResponse(upstream_conn_->to_transfer_data(),
                  upstream_conn_->to_transfer_bytes(), cb_wrap);
      LOG_DEBUG << "SingleGetCommand IsFirstCommand, call ForwardResponse, "
                << "resp=" << std::string(upstream_conn_->to_transfer_data(), upstream_conn_->to_transfer_bytes())
                << " to_transfer_bytes=" << upstream_conn_->to_transfer_bytes();
    } else {
      LOG_WARN << "SingleGetCommand IsFirstCommand, but is forwarding response, don't call ForwardResponse";
    }
  } else {
    // TODO : do nothing, just wait
    LOG_WARN << "SingleGetCommand IsFirstCommand false! to_transfer_bytes="
             << upstream_conn_->to_transfer_bytes();
  }

  if (!upstream_nomore_data()) {
    upstream_conn_->TryReadMoreData(); // upstream 正在read more的时候，不能memmove，不然写回的数据位置会相对漂移
  }
}

}

