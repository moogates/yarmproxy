#include "client_conn.h"

#include <atomic>
#include <chrono>
#include <memory>

#include "base/logging.h"

#include "allocator.h"
#include "command.h"
#include "error_code.h"
#include "read_buffer.h"
#include "worker_pool.h"

using namespace boost::asio;

namespace yarmproxy {

// TODO : gracefully close connections

ClientConnection::ClientConnection(WorkerContext& context)
  : socket_(context.io_service_)
  , buffer_(new ReadBuffer(context.allocator_->Alloc(), context.allocator_->slab_size()))
  , context_(context)
  , timer_(context.io_service_)
{
}

ClientConnection::~ClientConnection() {
  if (socket_.is_open()) {
    LOG_DEBUG << "ClientConnection destroyed close socket.";
    socket_.close();
  }
  context_.allocator_->Release(buffer_->data());
  delete buffer_;
}

void ClientConnection::IdleTimeout(const boost::system::error_code& ec) {
  if (ec == boost::asio::error::operation_aborted) {
    LOG_WARN << "ClientConnection IdleTimeout canceled.";
  } else {
    LOG_WARN << "ClientConnection IdleTimeout.";
    // timer was not cancelled, take necessary action.
  }
}

void ClientConnection::UpdateTimer() {
  // TODO : config timeout
  // timer_.expires_after(std::chrono::milliseconds(60000));
  // timer_.async_wait(std::bind(&ClientConnection::IdleTimeout, shared_from_this(), std::placeholders::_1));
}

void ClientConnection::StartRead() {
  boost::system::error_code ec;
  ip::tcp::no_delay nodelay(true);
  socket_.set_option(nodelay, ec);

  UpdateTimer();

  // if (!ec) {
  //   boost::asio::socket_base::linger linger(true, 0);
  //   socket_.set_option(linger, ec); // don't disable linger here
  // }

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
    LOG_DEBUG << "ClientConnection::AsyncRead do nothing";
    return;
  }
  LOG_DEBUG << "ClientConnection::AsyncRead begin";
  is_reading_more_ = true;
  buffer_->inc_recycle_lock();
  socket_.async_read_some(boost::asio::buffer(buffer_->free_space_begin(),
          buffer_->free_space_size()),
      std::bind(&ClientConnection::HandleRead, shared_from_this(),
          std::placeholders::_1, std::placeholders::_2));
}

void ClientConnection::RotateReplyingCommand() {
  if (active_cmd_queue_.size() == 1) {
    LOG_DEBUG << "RotateReplyingCommand AsyncRead when all commands processed";
    AsyncRead();
  }

  active_cmd_queue_.pop_front();
  if (!active_cmd_queue_.empty()) {
    active_cmd_queue_.front()->StartWriteReply();
    ProcessUnparsedQuery();
  }
}

void ClientConnection::WriteReply(const char* data, size_t bytes,
                                  const WriteReplyCallback& callback) {
  std::shared_ptr<ClientConnection> client_conn(shared_from_this());
  auto cb_wrap = [client_conn, data, bytes, callback](
      const boost::system::error_code& error, size_t bytes_transferred) {
    if (!error && bytes_transferred < bytes) {
      client_conn->WriteReply(data + bytes_transferred,
                              bytes - bytes_transferred, callback);
    } else {
      callback(error ? ErrorCode::E_WRITE_REPLY : ErrorCode::E_SUCCESS);
    }
  };

  boost::asio::async_write(socket_, boost::asio::buffer(data, bytes), cb_wrap);
}

bool ClientConnection::ProcessUnparsedQuery() {
  static const size_t PIPELINE_ACTIVE_LIMIT = 4;
  while(active_cmd_queue_.size() < PIPELINE_ACTIVE_LIMIT
        && buffer_->unparsed_received_bytes() > 0) {
    // TODO : close the conn if command line is  too long
    std::shared_ptr<Command> command;
    int parsed_bytes = Command::CreateCommand(shared_from_this(),
               buffer_->unprocessed_data(), buffer_->received_bytes(),
               &command);

    if (parsed_bytes < 0) {
      Abort();
      return false;
    }  else if (parsed_bytes == 0) {
      if (buffer_->unparsed_received_bytes() > 2048) {
        LOG_WARN << "Too long unparsable command line";
        return false;
      }
      TryReadMoreQuery();
      LOG_DEBUG << "ProcessUnparsedQuery waiting for more data";
      return true;
    } else {
      buffer_->update_parsed_bytes(parsed_bytes);

      command->WriteQuery();
      active_cmd_queue_.push_back(command);
      buffer_->update_processed_bytes(buffer_->unprocessed_bytes());

      if (!command->query_parsing_complete()) {
        break;
      }
    }
  }
  // TryReadMoreQuery();
  return true;
}

void ClientConnection::HandleRead(const boost::system::error_code& error,
                                  size_t bytes_transferred) {
  is_reading_more_ = false;
  buffer_->dec_recycle_lock();
  if (error) {
    if (error == boost::asio::error::eof) {
      // TODO : gracefully shutdown
      LOG_DEBUG << "ClientConnection::HandleRead eof error, conn=" << this;
    } else {
      LOG_WARN << "ClientConnection::HandleRead error=" << error.message()
               << " conn=" << this;
      Abort();
    }
    return;
  }

  UpdateTimer();

  buffer_->update_received_bytes(bytes_transferred);
  LOG_DEBUG << "HandleRead bytes_transferred=" << bytes_transferred
            << " parsed_unprocessed=" << buffer_->parsed_unprocessed_bytes();

  if (buffer_->parsed_unprocessed_bytes() > 0) {
    assert(!active_cmd_queue_.empty());

    active_cmd_queue_.back()->WriteQuery();
    buffer_->update_processed_bytes(buffer_->unprocessed_bytes());
    if (buffer_->parsed_unreceived_bytes() > 0) {
      // TryReadMoreQuery(); // TODO : 现在的做法是，这里不继续read, 而是在WriteQuery
                             // 的回调函数里面才继续read. 这并不是最佳方式
      return;
    }
  }

  // process the big bulk arrays in redis query
  if (!active_cmd_queue_.empty() &&
      !active_cmd_queue_.back()->query_parsing_complete()) {
    LOG_DEBUG << "ClientConnection::HandleRead ParseIncompleteQuery";
    active_cmd_queue_.back()->ParseIncompleteQuery();
  }

  if (active_cmd_queue_.empty() ||
      active_cmd_queue_.back()->query_parsing_complete()) {
    ProcessUnparsedQuery();
  }
  return;
}

void ClientConnection::Abort() {
  LOG_WARN << "ClientConnection::Abort client=" << this;
  timer_.cancel();
  active_cmd_queue_.clear();
  socket_.close();
}

}

