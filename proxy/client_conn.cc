#include "client_conn.h"

#include <atomic>
#include <memory>

#include "allocator.h"
#include "backend_conn.h"
#include "command.h"
#include "error_code.h"
#include "logging.h"
#include "read_buffer.h"
#include "worker_pool.h"

using namespace boost::asio;

namespace yarmproxy {

// TODO : gracefully close connections

std::atomic_int g_cc_count;

ClientConnection::ClientConnection(WorkerContext& context)
  : socket_(context.io_service_)
  , read_buffer_(new ReadBuffer(context.allocator_->Alloc(), context.allocator_->slab_size()))
  , context_(context)
  , timeout_(0) // TODO : timeout timer
  , timer_(context.io_service_)
{
  LOG_DEBUG << "ClientConnection created." << ++g_cc_count;
}

ClientConnection::~ClientConnection() {
  if (socket_.is_open()) {
    LOG_DEBUG << "ClientConnection destroyed close socket.";
    socket_.close();
  } else {
    LOG_DEBUG << "ClientConnection destroyed need not close socket.";
  }
  context_.allocator_->Release(read_buffer_->data());
  delete read_buffer_;
  LOG_DEBUG << "ClientConnection destroyed." << --g_cc_count;
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

void ClientConnection::TryReadMoreQuery() {
  // TODO : checking preconditions
  AsyncRead();
}

void ClientConnection::AsyncRead() {
  timer_.cancel();

  socket_.async_read_some(boost::asio::buffer(read_buffer_->free_space_begin(), read_buffer_->free_space_size()),
      std::bind(&ClientConnection::HandleRead, shared_from_this(),
          std::placeholders::_1, // 占位符
          std::placeholders::_2));
}

void ClientConnection::RotateReplyingCommand() {
  active_cmd_queue_.pop_front();
  if (!active_cmd_queue_.empty()) {
    active_cmd_queue_.front()->OnForwardReplyEnabled();
    ProcessUnparsedQuery();
  }
}

void ClientConnection::ForwardReply(const char* data, size_t bytes, const ForwardReplyCallback& cb) {
  // TODO : 成员函数化
  std::weak_ptr<ClientConnection> wptr(shared_from_this());
  auto cb_wrap = [wptr, data, bytes, cb](const boost::system::error_code& error, size_t bytes_transferred) {
    LOG_DEBUG << "ClientConnection::ForwardReply callback begin, bytes_transferred=" << bytes_transferred;
    if (!error && bytes_transferred < bytes) {
      if (auto ptr = wptr.lock()) {
        LOG_DEBUG << "ClientConnection::ForwardReply try write more, bytes_transferred=" << bytes_transferred
                 << " left_bytes=" << bytes - bytes_transferred << " conn=" << ptr.get();
        ptr->ForwardReply(data + bytes_transferred, bytes - bytes_transferred, cb);
      } else {
        LOG_DEBUG << "ClientConnection::ForwardReply try write more, but conn released";
      }
    } else {
      LOG_DEBUG << "ClientConnection::ForwardReply callback, bytes_transferred=" << bytes_transferred
               << " total_bytes=" << bytes << " error=" << error << "-" << error.message();
      if (error) { // TODO
      cb(ErrorCode::E_WRITE_REPLY);  // 发完了，或出错了，才告知Command
      } else {
      cb(ErrorCode::E_SUCCESS);  // 发完了，或出错了，才告知Command
      }
    }
  };

  boost::asio::async_write(socket_, boost::asio::buffer(data, bytes), cb_wrap);
}

void ClientConnection::Close() {
  // 对象是如何被销毁的?
  // active_cmd_queue_.clear();
  socket_.close();
}

bool ClientConnection::ProcessUnparsedQuery() {
  static const size_t PIPELINE_ACTIVE_LIMIT = 4;
  while(active_cmd_queue_.size() < PIPELINE_ACTIVE_LIMIT
        && read_buffer_->unparsed_received_bytes() > 0) {
    // TODO : close the conn if command line is  too long
    std::shared_ptr<Command> command;
    int parsed_bytes = Command::CreateCommand(shared_from_this(),
               read_buffer_->unprocessed_data(), read_buffer_->received_bytes(),
               &command);

    if (parsed_bytes < 0) {
      // TODO : error handling
      socket_.close();
      return false;
    }  else if (parsed_bytes == 0) {
      TryReadMoreQuery(); // read more data
      return true;
    } else {
      size_t to_process_bytes = std::min((size_t)parsed_bytes, read_buffer_->received_bytes());
      command->ForwardQuery(read_buffer_->unprocessed_data(), to_process_bytes);
      active_cmd_queue_.emplace_back(std::move(command));

      read_buffer_->update_parsed_bytes(parsed_bytes);
      read_buffer_->update_processed_bytes(to_process_bytes);
    }
  }
  return true;
}

void ClientConnection::HandleRead(const boost::system::error_code& error, size_t bytes_transferred) {
  if (error) {
    LOG_DEBUG << "ClientConnection::HandleRead error=" << error.message() << " conn=" << this;
    return;
  }

  read_buffer_->update_received_bytes(bytes_transferred);

  if (read_buffer_->parsed_unprocessed_bytes() > 0) {
    // 上次解析后，本次才接受到的数据
    active_cmd_queue_.back()->ForwardQuery(read_buffer_->unprocessed_data(), read_buffer_->unprocessed_bytes());
    read_buffer_->update_processed_bytes(read_buffer_->unprocessed_bytes());
    if (read_buffer_->parsed_unreceived_bytes() > 0) {
      // TryReadMoreQuery(); // 现在的做法是，这里不继续read, 而是在ForwardQuery的回调函数里面才继续read. 这并不是最佳方式
      return;
    }
  }

  ProcessUnparsedQuery();
  return;

//if (timeout_ > 0) {
//  // TODO : 改为每个command有一个timer
//  timer_.expires_from_now(boost::posix_time::millisec(timeout_));
//  // timer_.expires_from_now(std::chrono::milliseconds(timeout_));
//  timer_.async_wait(std::bind(&ClientConnection::HandleMemcCommandTimeout, shared_from_this(), std::placeholders::_1));
//}
}

void ClientConnection::HandleTimeoutWrite(const boost::system::error_code& error) {
  if (error) {
    socket_.close();
  }
}

void ClientConnection::HandleMemcCommandTimeout(const boost::system::error_code& error) {
  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      LOG_DEBUG << "ClientConnection::HandleMemcCommandTimeout timer error : " << error;
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
  char s[] = "SERVER_ERROR timeout\r\n";  // TODO : refining error message protocol
  boost::asio::async_write(socket_,
    boost::asio::buffer(s, sizeof(s) - 1),
      std::bind(&ClientConnection::HandleTimeoutWrite, shared_from_this(), std::placeholders::_1));
}

void ClientConnection::OnCommandError(std::shared_ptr<Command> cmd, ErrorCode ec) {
  // timer_.cancel();
  // TODO : 销毁工作
  // TODO : 如果是最后一个error, 要负责client的收尾工作
}

void ClientConnection::ErrorSilence() {
}

void ClientConnection::ErrorReport(const char* msg, size_t bytes) {
  // TODO : how to ensure send all?
  boost::asio::async_write(socket_, boost::asio::buffer(msg, bytes),
      std::bind(&ClientConnection::HandleTimeoutWrite, shared_from_this(), std::placeholders::_1));
  RotateReplyingCommand();
}

void ClientConnection::ErrorAbort() {
  active_cmd_queue_.clear(); // TODO : 是否足够? command dtor之后，内部backend的回调指针是否有效?
  socket_.close();
}

}

