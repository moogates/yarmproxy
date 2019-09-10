#include "client_conn.h"

#include <atomic>
#include <memory>

#include "allocator.h"
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
  , is_reading_more_(false)
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
  LOG_DEBUG << "ClientConnection destroyed." << --g_cc_count << " client=" << this;
}

void ClientConnection::StartRead() {
  boost::system::error_code ec;
  ip::tcp::no_delay nodelay(true);
  socket_.set_option(nodelay, ec);

  if (!ec) {
    boost::asio::socket_base::linger linger(true, 0);
    socket_.set_option(linger, ec);
  }

  if (ec) {
    socket_.close();
  } else {
    AsyncRead();
  }
}

void ClientConnection::TryReadMoreQuery() {
  // TODO : checking preconditions
  AsyncRead();
}

void ClientConnection::AsyncRead() {
  if (is_reading_more_) {
    LOG_WARN << "ClientConnection::AsyncRead do nothing";
    return;
  }
  LOG_WARN << "ClientConnection::AsyncRead begin";
  is_reading_more_ = true;
  read_buffer_->inc_recycle_lock();
  socket_.async_read_some(boost::asio::buffer(read_buffer_->free_space_begin(), read_buffer_->free_space_size()),
      std::bind(&ClientConnection::HandleRead, shared_from_this(),
          std::placeholders::_1, // 占位符
          std::placeholders::_2));
}

void ClientConnection::RotateReplyingCommand() {
  active_cmd_queue_.pop_front();
  if (!active_cmd_queue_.empty()) {
    active_cmd_queue_.front()->StartWriteReply();
    ProcessUnparsedQuery();
  }
}

void ClientConnection::WriteReply(const char* data, size_t bytes, const WriteReplyCallback& callback) {
  // TODO : 成员函数化
  std::shared_ptr<ClientConnection> ptr(shared_from_this());
  auto cb_wrap = [ptr, data, bytes, callback](const boost::system::error_code& error, size_t bytes_transferred) {
    if (!error && bytes_transferred < bytes) {
      LOG_WARN << "Command::TryWriteReply callback, write more, bytes_to_transfer=" << bytes
            << " bytes_transferred=" << bytes_transferred;
      ptr->WriteReply(data + bytes_transferred, bytes - bytes_transferred, callback);
    } else {
      // LOG_WARN << "Command::TryWriteReply callback, error=" << error
      //       << " bytes_transferred=" << bytes_transferred;
      // 发完了，或出错了，才告知Command
      callback(error ? ErrorCode::E_WRITE_REPLY : ErrorCode::E_SUCCESS);
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
      Abort();
      return false;
    }  else if (parsed_bytes == 0) {
      if (read_buffer_->unparsed_received_bytes() > 128) {
        LOG_WARN << "ClientConnection::HandleRead too long unparsable command line";
        return false;
      }
      TryReadMoreQuery(); // read more data
      LOG_WARN << "ClientConnection::HandleRead break for more data";
      return true;
    } else {
      read_buffer_->update_parsed_bytes(parsed_bytes);
      size_t to_process_bytes = std::min((size_t)parsed_bytes, read_buffer_->received_bytes());
      assert(to_process_bytes == read_buffer_->unprocessed_bytes());

      command->WriteQuery();
      active_cmd_queue_.emplace_back(std::move(command));

      read_buffer_->update_processed_bytes(to_process_bytes);
    }
  }
  LOG_DEBUG << "ClientConnection::HandleRead active_cmd_queue_.size=" << active_cmd_queue_.size();
  // TryReadMoreQuery(); // TODO : read should continues here, so that the conn shared_ptr won't be released
  return true;
}

void ClientConnection::HandleRead(const boost::system::error_code& error, size_t bytes_transferred) {
  is_reading_more_ = false;
  read_buffer_->dec_recycle_lock();
  if (bytes_transferred == 0) {
    abort();
    // exit(0);
  }

  LOG_WARN << "ClientConnection::AsyncRead done";
  if (error) {
    LOG_DEBUG << "ClientConnection::HandleRead error=" << error.message() << " conn=" << this;
    return;
  }

  read_buffer_->update_received_bytes(bytes_transferred);
  LOG_WARN << "ClientConnection::HandleRead bytes_transferred=" << bytes_transferred
            << " parsed_unprocessed_bytes=" << read_buffer_->parsed_unprocessed_bytes();

  if (read_buffer_->parsed_unprocessed_bytes() > 0) {
    // 上次解析后，本次才接受到的数据
    active_cmd_queue_.back()->WriteQuery();
    read_buffer_->update_processed_bytes(read_buffer_->unprocessed_bytes());
    if (read_buffer_->parsed_unreceived_bytes() > 0) {
      // TryReadMoreQuery(); // TODO : 现在的做法是，这里不继续read, 而是在WriteQuery的回调函数里面才继续read. 这并不是最佳方式
      return;
    }
  }

  // process the big bulk arrays in redis query
  if (!active_cmd_queue_.empty() && !active_cmd_queue_.back()->QueryParsingComplete()) {
    LOG_WARN << "ClientConnection::HandleRead ParseIncompleteQuery";
    active_cmd_queue_.back()->ParseIncompleteQuery();
  }

  if (active_cmd_queue_.empty() || active_cmd_queue_.back()->QueryParsingComplete()) { // 避免从bulk array中间开始解析新指令
    ProcessUnparsedQuery();
  }
  return;
}

void ClientConnection::HandleTimeoutWrite(const boost::system::error_code& error) {
  if (error) {
    socket_.close();
  }
}

void ClientConnection::Abort() {
  LOG_DEBUG << "ClientConnection::Abort client=" << this;
  active_cmd_queue_.clear(); // TODO : 是否足够? command dtor之后，内部backend的回调指针是否有效?
  socket_.close();
}

}

