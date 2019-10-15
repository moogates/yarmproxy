#include "client_conn.h"

#include <atomic>
#include <chrono>
#include <memory>

#include "logging.h"

#include "allocator.h"
#include "config.h"
#include "command.h"
#include "error_code.h"
#include "read_buffer.h"
#include "stats.h"
#include "worker_pool.h"

namespace yarmproxy {

ClientConnection::ClientConnection(WorkerContext& context)
    : socket_(context.io_service_)
    , buffer_(new ReadBuffer(context.allocator_->Alloc(),
                      context.allocator_->slab_size()))
    , context_(context)
    , read_timer_(context.io_service_)
    , write_timer_(context.io_service_) {
  ++g_stats_.client_conns_;
  LOG_DEBUG << "client ctor. count=" << g_stats_.client_conns_;
}

ClientConnection::~ClientConnection() {
  if (socket_.is_open()) {
    LOG_DEBUG << "client destroyed close socket.";
    socket_.close();
  }
  context_.allocator_->Release(buffer_->data());
  delete buffer_;
  --g_stats_.client_conns_;
  LOG_DEBUG << "client dtor. count=" << g_stats_.client_conns_;
}

void ClientConnection::OnTimeout(const boost::system::error_code& ec,
                                 TimerType timer_type) {
  if (ec != boost::asio::error::operation_aborted) {
    // timer was not cancelled, take necessary action.
    LOG_WARN << "client OnTimeout, timer=" << timer_type;
    if (timer_type == READ_TIMER) {
      ++g_stats_.client_read_timeouts_;
    } else {
      ++g_stats_.client_write_timeouts_;
    }
    Abort();
  }
}

void ClientConnection::UpdateTimer(TimerType timer_type) {
  if (aborted_) {
    return;
  }
  boost::asio::steady_timer& timer = 
      timer_type == READ_TIMER ? read_timer_ : write_timer_;

  int timeout = (timer_type == READ_TIMER && 
      buffer_->unparsed_received_bytes() == 0) ?
      Config::Instance().client_idle_timeout() :
      Config::Instance().socket_rw_timeout();

  timer.expires_after(std::chrono::milliseconds(timeout));
  LOG_DEBUG << "client UpdateTimer " << timer_type << " timeout=" << timeout;

  std::weak_ptr<ClientConnection> wptr(shared_from_this());
  timer.async_wait([wptr, timer_type](const boost::system::error_code& ec) {
        if (auto ptr = wptr.lock()) {
          ptr->OnTimeout(ec, timer_type);
        }
      });
}

void ClientConnection::StartRead() {
  boost::system::error_code ec;
  boost::asio::ip::tcp::no_delay nodelay(true);
  socket_.set_option(nodelay, ec);

  if (ec) {
    LOG_WARN << "client StartRead set socket option error";
    socket_.close();
  } else {
    LOG_ERROR << "client StartRead ===================================";
    AsyncRead();
  }
}

void ClientConnection::TryReadMoreQuery(const char* caller) {
  LOG_DEBUG << "client TryReadMoreQuery caller=" << caller
       << " buffer_lock=" << buffer_->recycle_lock_count()
       << " is_reading_query=" << is_reading_query_
       << " has_much_free_space=" << buffer_->has_much_free_space()
       << " free_space=" << buffer_->free_space_size();
  if (is_reading_query_ || !buffer_->has_much_free_space()) {
    return;
  }
  AsyncRead();
}

void ClientConnection::AsyncRead() {
  is_reading_query_ = true;
  buffer_->inc_recycle_lock();

  LOG_DEBUG << "client AsyncRead, buffer=" << buffer_
            << " free_space=" << buffer_->free_space_size()
            << " lock=" << buffer_->recycle_lock_count();

  UpdateTimer(READ_TIMER);
  socket_.async_read_some(boost::asio::buffer(
      buffer_->free_space_begin(), buffer_->free_space_size()),
      std::bind(&ClientConnection::HandleRead, shared_from_this(),
          std::placeholders::_1, std::placeholders::_2));
}

void ClientConnection::RotateReplyingCommand() {
  if (active_cmd_queue_.size() == 1) {
    // read before pop to avoid deref
    TryReadMoreQuery("client_conn_5");
  }

  active_cmd_queue_.pop_front();

  if (!active_cmd_queue_.empty()) {
    active_cmd_queue_.front()->StartWriteReply();

    if (!active_cmd_queue_.back()->query_recv_complete()) {
      if (!buffer_->recycle_locked()) {
        // TODO : reading next query might be blocked by previous
        // command's lock, so try to restart the reading here
        TryReadMoreQuery("client_conn_1");
      }
    } else {
      ProcessUnparsedQuery();
    }
  }
}

void ClientConnection::WriteReply(const char* data, size_t bytes,
                                  const WriteReplyCallback& callback) {
  if (is_writing_reply_) {
    assert(false);
  }
  std::shared_ptr<ClientConnection> client_conn(shared_from_this());
  auto cb_wrap = [client_conn, data, bytes, callback](
      const boost::system::error_code& error, size_t bytes_transferred) {
    client_conn->write_timer_.cancel();
    if (!error) {
      g_stats_.bytes_to_clients_ += bytes_transferred;
    }
    if (!error && bytes_transferred < bytes) {
      client_conn->WriteReply(data + bytes_transferred,
                              bytes - bytes_transferred, callback);
    } else {
      client_conn->is_writing_reply_ = false;
      callback(error ? ErrorCode::E_WRITE_REPLY : ErrorCode::E_SUCCESS);
    }
  };

  is_writing_reply_ = true;
  UpdateTimer(WRITE_TIMER);
  boost::asio::async_write(socket_, boost::asio::buffer(data, bytes), cb_wrap);
}

void ClientConnection::ProcessUnparsedQuery() {
  // TODO : pipeline中多个请求的时序问题,即后面的command可能先
  // 被执行. 参考 del_pipeline_1.sh
  static const size_t PIPELINE_ACTIVE_LIMIT = 4; // TODO : config
  while(active_cmd_queue_.size() < PIPELINE_ACTIVE_LIMIT
        && buffer_->unparsed_received_bytes() > 0) {
    std::shared_ptr<Command> command;
    size_t parsed_bytes = Command::CreateCommand(shared_from_this(),
               buffer_->unprocessed_data(), buffer_->received_bytes(),
               &command);

    if (parsed_bytes == 0) {
      TryReadMoreQuery("client_conn_2");
      break;
    }
    buffer_->update_parsed_bytes(parsed_bytes);

    active_cmd_queue_.push_back(command);
    bool no_callback = command->StartWriteQuery(); // rename to StartWriteQuery
    buffer_->update_processed_bytes(buffer_->unprocessed_bytes());

    // TODO : check the precondition very carefully
    if (buffer_->parsed_unreceived_bytes() > 0 ||
        !command->query_parsing_complete()) {
      if (no_callback) {
        TryReadMoreQuery("client_conn_3");
      }
      LOG_DEBUG << "ProcessUnparsedQuery break. parsed_bytes=" << parsed_bytes;
      break;
    }
  }
}

void ClientConnection::HandleRead(const boost::system::error_code& error,
                                  size_t bytes_transferred) {
  read_timer_.cancel();
  if (aborted_) {
    return;
  }
  is_reading_query_ = false;

  if (error) {
    if (error == boost::asio::error::eof) {
      // TODO : gracefully shutdown
      LOG_DEBUG << "client HandleRead eof error, conn=" << this;
    } else {
      LOG_DEBUG << "client HandleRead error=" << error.message()
               << " conn=" << this;
      Abort();
    }
    return;
  }

  g_stats_.bytes_from_clients_ += bytes_transferred;
  buffer_->update_received_bytes(bytes_transferred);
  buffer_->dec_recycle_lock();

  LOG_DEBUG << "client HandleRead buffer=" << buffer_
            << " bytes_transferred=" << bytes_transferred
            << " parsed_unprocessed=" << buffer_->parsed_unprocessed_bytes();
  std::shared_ptr<Command> back_cmd;
  if (!active_cmd_queue_.empty()) {
    back_cmd = active_cmd_queue_.back();
  }

  if (back_cmd && !back_cmd->query_recv_complete()) {
    if (!back_cmd->ParseUnparsedPart()) {
      LOG_DEBUG << "client ParseUnparsedPart error";
      Abort();
      return;
    }
  }

  if (buffer_->parsed_unprocessed_bytes() > 0) {
    assert(back_cmd);

    // TODO : split to StartWriteQuery()/WriteEarlierParsedQuery()
    bool no_callback = back_cmd->ContinueWriteQuery();
    buffer_->update_processed_bytes(buffer_->unprocessed_bytes());

    if (buffer_->parsed_unreceived_bytes() > 0) {
      // 这里不继续read, 而是在ContinueWriteQuery的回调函数里面才
      // 继续read (or read directly if ContinueWriteQuery has no
      // callback). 理论上这并不是最佳方式.
      if (no_callback) {
        TryReadMoreQuery("client_conn_4");
      }
      return;
    }
  }

  // process the big bulk arrays in redis query
  if (back_cmd && !back_cmd->query_parsing_complete()) {
    LOG_DEBUG << "client HandleRead ProcessUnparsedPart";
    if (!back_cmd->ProcessUnparsedPart()) {
      LOG_DEBUG << "client HandleRead ProcessUnparsedPart Abort";
      Abort();
      return;
    }
  }

  if (!back_cmd || back_cmd->query_parsing_complete()) {
    ProcessUnparsedQuery();
  }
}

void ClientConnection::Abort() {
  if (aborted_) {
    return;
  }
  LOG_WARN << "client " << this << " Abort";

  aborted_ = true;
  read_timer_.cancel();
  write_timer_.cancel();
  socket_.close();

  // keep this line at end to avoid earyly deref.
  active_cmd_queue_.clear();
}

}

