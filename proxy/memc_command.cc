#include "memc_command.h"

#include <vector>
#include <functional>

#include <boost/asio.hpp>

#include "base/logging.h"

#include "client_conn.h"
#include "upstream_conn.h"

namespace mcproxy {

ForwardResponseCallback WrapOnForwardResponseFinished(size_t to_transfer_bytes, std::weak_ptr<MemcCommand> cmd_wptr);

//存储命令 : <command name> <key> <flags> <exptime> <bytes>\r\n
MemcCommand::MemcCommand(boost::asio::io_service& io_service, const ip::tcp::endpoint & ep, 
    std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len) 
  : is_forwarding_response_(false)
  , upstream_endpoint_(ep)
  , upstream_conn_(nullptr)
  , client_conn_(owner)
  , io_service_(io_service)
  , loaded_(false)
  , upstream_nomore_data_(false)
{
  upstream_conn_ = owner->upconn_pool()->Pop(upstream_endpoint_);
};

void MemcCommand::OnUpstreamResponse(const boost::system::error_code& error) {
  if (error) {
    LOG_WARN << "MemcCommand::OnUpstreamResponse " << cmd_line_without_rn()
             << " upstream read error : " << upstream_endpoint_ << " - "  << error << " " << error.message();
    client_conn_->OnCommandError(shared_from_this(), error);
    return;
  }
  LOG_DEBUG << "SingleGetCommand OnUpstreamResponse data";

  bool valid = ParseUpstreamResponse();
  if (!valid) {
    LOG_WARN << "SingleGetCommand parsing error! valid=false";
    // TODO : error handling
  }
  if (IsFormostCommand()) {
    if (!is_forwarding_response_) {
      is_forwarding_response_ = true; // TODO : 这个flag是否真的需要? 需要，防止重复的写回请求
      auto cb_wrap = WrapOnForwardResponseFinished(upstream_conn_->read_buffer_.to_transfer_bytes(), shared_from_this());
      client_conn_->ForwardResponse(upstream_conn_->read_buffer_.to_transfer_data(),
                  upstream_conn_->read_buffer_.to_transfer_bytes(), cb_wrap);
      LOG_DEBUG << "SingleGetCommand IsFirstCommand, call ForwardResponse, to_transfer_bytes="
                << upstream_conn_->read_buffer_.to_transfer_bytes();
    } else {
      LOG_WARN << "SingleGetCommand IsFirstCommand, but is forwarding response, don't call ForwardResponse";
    }
  } else {
    // TODO : do nothing, just wait
    LOG_WARN << "SingleGetCommand IsFirstCommand false! to_transfer_bytes="
             << upstream_conn_->read_buffer_.to_transfer_bytes();
  }

  if (!upstream_nomore_data()) {
    upstream_conn_->TryReadMoreData(); // upstream 正在read more的时候，不能memmove，不然写回的数据位置会相对漂移
  }
}

MemcCommand::~MemcCommand() {
  if (upstream_conn_) {
    delete upstream_conn_; // TODO : 需要连接池。暂时直接销毁
  }
}

UpstreamReadCallback WrapOnUpstreamResponse(std::weak_ptr<MemcCommand> cmd_wptr);
UpstreamWriteCallback WrapOnUpstreamRequestWritten(std::weak_ptr<MemcCommand> cmd_wptr);

bool MemcCommand::IsFormostCommand() {
  return client_conn_->IsFirstCommand(shared_from_this());
}

void MemcCommand::Abort() {
  if (upstream_conn_) {
    upstream_conn_->socket().close();
    // MCE_INFO("MemcCommand Abort OK.");
    delete upstream_conn_;
    upstream_conn_ = 0;
  } else {
    // MCE_WARN("MemcCommand Abort NULL upstream_conn_.");
  }
}

void MemcCommand::OnForwardResponseFinished(size_t bytes, const boost::system::error_code& error) {
  if (error) {
    // TODO
    LOG_DEBUG << "WriteCommand::OnForwardResponseFinished (" << cmd_line_without_rn() << ") error=" << error;
    return;
  }

  upstream_conn_->read_buffer_.update_transfered_bytes(bytes);

  if (upstream_nomore_data() && upstream_conn_->read_buffer_.to_transfer_bytes() == 0) {
    client_conn_->RotateFirstCommand();
    LOG_DEBUG << "WriteCommand::OnForwardResponseFinished upstream_nomore_data, and all data forwarded to client";
  } else {
    LOG_DEBUG << "WriteCommand::OnForwardResponseFinished upstream transfered_bytes=" << bytes
              << " ready_to_transfer_bytes=" << upstream_conn_->read_buffer_.to_transfer_bytes();
    is_forwarding_response_ = false;
    if (!upstream_nomore_data()) {
      upstream_conn_->TryReadMoreData(); // 这里必须继续try
    }

    OnForwardResponseReady(); // 可能已经有新读到的数据，因而要尝试转发更多
  }
}

}

