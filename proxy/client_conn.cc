#include "client_conn.h"

#include <atomic>
#include <memory>

#include "base/logging.h"

#include "worker_pool.h"
#include "memc_command.h"
#include "backend_conn.h"

using namespace boost::asio;

namespace mcproxy {

// TODO :
// 1. gracefully close connections
// 2. 

std::atomic_int g_cc_count;

ClientConnection::ClientConnection(WorkerContext& context)
  : socket_(context.io_service_)
  , context_(context)
  , timeout_(0) // TODO : timeout timer 
  , timer_(context.io_service_)
{
  LOG_INFO << "ClientConnection created." << ++g_cc_count;
}

ClientConnection::~ClientConnection() {
  if (socket_.is_open()) {
    LOG_INFO << "ClientConnection destroyed close socket."; 
    socket_.close();
  } else {
    LOG_INFO << "ClientConnection destroyed need not close socket.";
  }
  LOG_INFO << "ClientConnection destroyed." << --g_cc_count;
}

void ClientConnection::StartRead() {
  ip::tcp::no_delay nodelay(true);
  socket_.set_option(nodelay);

  boost::asio::socket_base::linger linger(true, 0);
  socket_.set_option(linger);

  boost::asio::socket_base::send_buffer_size send_buf_size(48 * 1024);
  socket_.set_option(send_buf_size);

  AsyncRead();
}

void ClientConnection::TryReadMoreRequest() {
  // TODO : add preconditions check
  AsyncRead();
}

void ClientConnection::AsyncRead() {
  timer_.cancel();

  socket_.async_read_some(boost::asio::buffer(read_buffer_.free_space_begin(), read_buffer_.free_space_size()),
      std::bind(&ClientConnection::HandleRead, shared_from_this(),
          std::placeholders::_1, // 占位符
          std::placeholders::_2));
}

void ClientConnection::RotateFirstCommand() {
  poly_cmd_queue_.pop_front();
  if (!poly_cmd_queue_.empty()) {
    LOG_DEBUG << __func__ << " poly_cmd_queue_.size=" << poly_cmd_queue_.size();
    poly_cmd_queue_.front()->OnForwardReplyEnabled();
  } else {
    LOG_DEBUG << __func__ << " poly_cmd_queue_ emptr";
  }
}

void ClientConnection::ForwardResponse(const char* data, size_t bytes, const ForwardResponseCallback& cb) {
  forward_resp_callback_ = cb;

  // TODO : 成员函数化
  std::weak_ptr<ClientConnection> wptr(shared_from_this());
  auto cb_wrap = [wptr, data, bytes, cb](const boost::system::error_code& error, size_t bytes_transferred) {
    LOG_DEBUG << "ClientConnection::ForwardResponse callback begin, bytes_transferred=" << bytes_transferred;
    if (!error && bytes_transferred < bytes) {
      if (auto ptr = wptr.lock()) {
        LOG_DEBUG << "ClientConnection::ForwardResponse try write more, bytes_transferred=" << bytes_transferred
                 << " left_bytes=" << bytes - bytes_transferred << " conn=" << ptr.get();
        ptr->ForwardResponse(data + bytes_transferred, bytes - bytes_transferred, cb);
      } else {
        LOG_DEBUG << "ClientConnection::ForwardResponse try write more, but conn released";
      }
    } else {
      LOG_DEBUG << "ClientConnection::ForwardResponse callback, bytes_transferred=" << bytes_transferred
               << " total_bytes=" << bytes << " error=" << error << "-" << error.message();
      cb(error);  // 发完了，或出错了，才告知MemcCommand
    }
  };

  boost::asio::async_write(socket_, boost::asio::buffer(data, bytes), cb_wrap);
}

void ClientConnection::HandleRead(const boost::system::error_code& error, size_t bytes_transferred) {
  if (error) {
    LOG_INFO << "ClientConnection::HandleRead error=" << error.message() << " conn=" << this;
    return;
  }

  read_buffer_.update_received_bytes(bytes_transferred);

  if (read_buffer_.parsed_unprocessed_bytes() > 0) {
    // 上次解析后，本次才接受到的数据
    poly_cmd_queue_.back()->ForwardRequest(read_buffer_.unprocessed_data(), read_buffer_.unprocessed_bytes());
    read_buffer_.update_processed_bytes(read_buffer_.unprocessed_bytes());
    if (read_buffer_.parsed_unreceived_bytes() > 0) {
      // TODO : 要不要AsyncRead() ?
      return;
    }
  }

  while(read_buffer_.unparsed_received_bytes() > 0) {
    std::shared_ptr<MemcCommand> command;
    int parsed_bytes = MemcCommand::CreateCommand(shared_from_this(),
               read_buffer_.unprocessed_data(), read_buffer_.received_bytes(),
               &command);

    if (parsed_bytes < 0) {
      // TODO : error handling
      socket_.close();
      return;
    }  else if (parsed_bytes == 0) {
      AsyncRead(); // read more data
      return;
    } else {
      size_t to_process_bytes = std::min((size_t)parsed_bytes, read_buffer_.received_bytes());
      command->ForwardRequest(read_buffer_.unprocessed_data(), to_process_bytes);  
      // poly_cmd_queue_.splice(poly_cmd_queue_.end(), sub_commands);
      poly_cmd_queue_.emplace_back(std::move(command));// TODO : 要控制单client的并发command数

      read_buffer_.update_parsed_bytes(parsed_bytes);
      read_buffer_.update_processed_bytes(to_process_bytes);
    }
  }

  if (timeout_ > 0) {
    // TODO : 改为每个command有一个timer
    timer_.expires_from_now(boost::posix_time::millisec(timeout_));
    // timer_.expires_from_now(std::chrono::milliseconds(timeout_));
    timer_.async_wait(std::bind(&ClientConnection::HandleMemcCommandTimeout, shared_from_this(), std::placeholders::_1));
  }
}

void ClientConnection::HandleTimeoutWrite(const boost::system::error_code& error) {
  if (error) {
    socket_.close();
  }
}

void ClientConnection::HandleMemcCommandTimeout(const boost::system::error_code& error) {
  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      LOG_INFO << "ClientConnection::HandleMemcCommandTimeout timer error : " << error;
    }
    return;
  }

  try {
    // static char s[] = "SERVER_ERROR timeout\r\n";
    static char s[] = "xEND\r\n"; // TODO : 标准协议是怎样的呢?
    boost::asio::write(socket_, boost::asio::buffer(s, sizeof(s) - 1));
  } catch(std::exception& e) {
    LOG_WARN << "ClientConnection::HandleMemcCommandTimeout write END err, " << e.what();
  }

  socket_.close();
  return;
  char s[] = "SERVER_ERROR timeout\r\n";
  boost::asio::async_write(socket_,
    boost::asio::buffer(s, sizeof(s) - 1),
      std::bind(&ClientConnection::HandleTimeoutWrite, shared_from_this(), std::placeholders::_1));
}

void ClientConnection::OnCommandError(std::shared_ptr<MemcCommand> memc_cmd, const boost::system::error_code& error) {
  timer_.cancel();
  // TODO : 销毁工作
  // TODO : 如果是最后一个error, 要负责client的收尾工作
}

}

