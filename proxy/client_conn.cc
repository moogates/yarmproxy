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

// TODO : gracefully close connections

ClientConnection::ClientConnection(WorkerContext& context)
    : socket_(context.io_service_)
    , buffer_(new ReadBuffer(context.allocator_->Alloc(),
                      context.allocator_->slab_size()))
    , context_(context)
    , timer_(context.io_service_) {
  ++g_stats_.client_conns_;
}

ClientConnection::~ClientConnection() {
  LOG_WARN << "ClientConnection dtor.";
  if (socket_.is_open()) {
    LOG_DEBUG << "ClientConnection destroyed close socket.";
    socket_.close();
  }
  context_.allocator_->Release(buffer_->data());
  delete buffer_;
  --g_stats_.client_conns_;
}

void ClientConnection::OnTimeout(const boost::system::error_code& ec) {
  // assert(!aborted_);
  if (ec != boost::asio::error::operation_aborted) {
    // timer was not cancelled, take necessary action.
    LOG_WARN << "ClientConnection OnTimeout.";
    if (!aborted_) {
      Abort();
    }
  }
}

void ClientConnection::UpdateTimer() {
  assert(!aborted_);
  // TODO : 细致的超时处理, 包括connect/read/write/command
  ++timer_ref_count_;

  int timeout = active_cmd_queue_.empty() && 
      buffer_->unparsed_received_bytes() == 0 ?
          Config::Instance().client_idle_timeout() :
          (Config::Instance().command_exec_timeout() + 10);
  size_t canceled = timer_.expires_after(std::chrono::milliseconds(timeout));
  LOG_DEBUG << "ClientConnection UpdateTimer timeout=" << timeout
           << " canceled=" << canceled;

  std::weak_ptr<ClientConnection> wptr(shared_from_this());
  timer_.async_wait([wptr](const boost::system::error_code& ec) {
        if (auto ptr = wptr.lock()) {
          ptr->OnTimeout(ec);
        }
      });
}

void ClientConnection::RevokeTimer() {
  assert(timer_ref_count_ > 0);
  if (--timer_ref_count_ == 0) {
    try {
    timer_.cancel();
    } catch(...) {
      abort();
    }
  }
}

void ClientConnection::StartRead() {
  boost::system::error_code ec;
  boost::asio::ip::tcp::no_delay nodelay(true);
  socket_.set_option(nodelay, ec);

  if (ec) {
    LOG_WARN << "ClientConn StartRead set socket option error";
    socket_.close();
  } else {
    LOG_ERROR << "ClientConn StartRead ===================================";
    AsyncRead();
  }
}

void ClientConnection::TryReadMoreQuery(const char* caller) {
  // TODO : checking preconditions
  LOG_DEBUG << "ClientConnection::TryReadMoreQuery caller=" << caller
           << " buffer_lock=" << buffer_->recycle_lock_count();
  AsyncRead();
}

void ClientConnection::AsyncRead() {
  if (is_reading_query_) {
    LOG_DEBUG << "ClientConnection::AsyncRead busy, do nothing";
    return;
  }
  if (buffer_->free_space_size() < 512) {
    LOG_DEBUG << "ClientConnection::AsyncRead no free space, do nothing"
               << " lock=" << buffer_->recycle_lock_count()
               << " received_bytes=" << buffer_->received_bytes();
    return;
  }
  LOG_DEBUG << "ClientConnection::AsyncRead begin,"
               << " lock=" << buffer_->recycle_lock_count()
               << " received_unprocess_bytes=" << buffer_->received_bytes();

  is_reading_query_ = true;
  buffer_->inc_recycle_lock();

  LOG_DEBUG << "ClientConnection::AsyncRead begin, free_space_size="
            << buffer_->free_space_size()
            << " buffer=" << buffer_;

  assert(buffer_->recycle_locked());
  UpdateTimer();
  socket_.async_read_some(boost::asio::buffer(
      buffer_->free_space_begin(), buffer_->free_space_size()),
      std::bind(&ClientConnection::HandleRead, shared_from_this(),
          std::placeholders::_1, std::placeholders::_2));
}

void ClientConnection::RotateReplyingCommand() {
  if (active_cmd_queue_.size() == 1) {
    LOG_DEBUG << "RotateReplyingCommand AsyncRead when all commands processed";
    // AsyncRead();
    TryReadMoreQuery("client_conn_5");
  }

  LOG_DEBUG << "RotateReplyingCommand old_command=" << active_cmd_queue_.front();
  active_cmd_queue_.pop_front();
  if (!active_cmd_queue_.empty()) {
    // active_cmd_queue_.front()->StartWriteReply();
    auto& next = active_cmd_queue_.front();
    next->StartWriteReply();

    if (!active_cmd_queue_.back()->query_recv_complete()) {
      if (!buffer_->recycle_locked()) {
        // TODO : reading next query might be interruptted by previous lock,
        // so try to restart the reading here
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
    client_conn->RevokeTimer();
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
  UpdateTimer();
  boost::asio::async_write(socket_, boost::asio::buffer(data, bytes), cb_wrap);
}

bool ClientConnection::ProcessUnparsedQuery() {
  // TODO : pipeline中多个请求的时序问题,即后面的command可能先
  // 被执行. 参考 del_pipeline_1.sh
  static const size_t PIPELINE_ACTIVE_LIMIT = 4;
  while(active_cmd_queue_.size() < PIPELINE_ACTIVE_LIMIT
        && buffer_->unparsed_received_bytes() > 0) {
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
        // Abort(); // TODO : test it
        return false;
      }
      TryReadMoreQuery("client_conn_2");
      return true;
    } else {
      buffer_->update_parsed_bytes(parsed_bytes);

      active_cmd_queue_.push_back(command);
      bool no_callback = command->WriteQuery(); // rename to StartWriteQuery
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
  // TryReadMoreQuery();
  return true;
}

void ClientConnection::HandleRead(const boost::system::error_code& error,
                                  size_t bytes_transferred) {
  RevokeTimer();
  if (aborted_) {
    // backend interrrupted
    return;
  }
  is_reading_query_ = false;

  if (error) {
    if (error == boost::asio::error::eof) {
      // TODO : gracefully shutdown
      LOG_DEBUG << "ClientConnection::HandleRead eof error, conn=" << this;
    } else {
      LOG_DEBUG << "ClientConnection::HandleRead error=" << error.message()
               << " conn=" << this;
      Abort();
    }
    return;
  }

  g_stats_.bytes_from_clients_ += bytes_transferred;
  buffer_->update_received_bytes(bytes_transferred);
  buffer_->dec_recycle_lock();

  LOG_DEBUG << "HandleRead bytes_transferred=" << bytes_transferred
            << " buffer=" << buffer_
            << " parsed_unprocessed=" << buffer_->parsed_unprocessed_bytes();

  if (!active_cmd_queue_.empty() && !active_cmd_queue_.back()->query_recv_complete()) {
    LOG_DEBUG << "ClientConnection::HandleRead ParseUnparsedPart";
    if (!active_cmd_queue_.back()->ParseUnparsedPart()) {
      LOG_DEBUG << "ClientConnection::HandleRead ParseUnparsedPart Abort";
      Abort();
      return;
    }
  }

  // TODO :  add var back_cmd
  if (buffer_->parsed_unprocessed_bytes() > 0) {
    assert(!active_cmd_queue_.empty());

    // bool no_callback = active_cmd_queue_.back()->ContinueWriteQuery(); // TODO : split to WriteNewParsedQuery()/WriteEarlierParsedQuery()
    bool no_callback = active_cmd_queue_.back()->WriteQuery(); // TODO : split to WriteNewParsedQuery()/WriteEarlierParsedQuery()
    buffer_->update_processed_bytes(buffer_->unprocessed_bytes());

    if (buffer_->parsed_unreceived_bytes() > 0) {
      // TODO : 这里不继续read, 而是在ContinueWriteQuery的回调函数里面才继续read
      // (or ContinueWriteQuery has no callback). 理论上这并不是最佳方式
      if (no_callback) {
        TryReadMoreQuery("client_conn_4");
      }
      return;
    }
  }

  // process the big bulk arrays in redis query
  if (!active_cmd_queue_.empty() &&
      !active_cmd_queue_.back()->query_parsing_complete()) {
    LOG_DEBUG << "ClientConnection::HandleRead ProcessUnparsedPart";
    if (!active_cmd_queue_.back()->ProcessUnparsedPart()) {
      LOG_DEBUG << "ClientConnection::HandleRead ProcessUnparsedPart Abort";
      Abort();
      return;
    }
  }


  if (active_cmd_queue_.empty() ||
      active_cmd_queue_.back()->query_parsing_complete()) {
    ProcessUnparsedQuery();
  }
}

void ClientConnection::Abort() {
  LOG_WARN << "ClientConnection::Abort client=" << this;
  aborted_ = true;
//if (timer_ref_count_ > 0) {
//  timer_.cancel();
//}
  timer_.cancel();
  active_cmd_queue_.clear();
  socket_.close();
}

}

