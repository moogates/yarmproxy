#include "backend_conn.h"

#include "logging.h"

#include "allocator.h"
#include "config.h"
#include "error_code.h"
#include "read_buffer.h"
#include "stats.h"
#include "worker_pool.h"

namespace yarmproxy {
// TODO: 更好的容错，错误时更有好的返回信息

BackendConn::BackendConn(WorkerContext& context,
      const Endpoint& endpoint)
    : context_(context)
    , buffer_(new ReadBuffer(context.allocator_->Alloc(),
          context.allocator_->buffer_size()))
    , remote_endpoint_(endpoint)
    , socket_(context.io_service_)
    , write_timer_(context.io_service_)
    , read_timer_(context.io_service_) {
  ++g_stats_.backend_conns_;
  LOG_DEBUG << "BackendConn ctor, count=" << g_stats_.backend_conns_;
}

BackendConn::~BackendConn() {
  --g_stats_.backend_conns_;
  LOG_DEBUG << "BackendConn " << this << " dtor, count=" << g_stats_.backend_conns_;
  if (socket_.is_open()) {
    socket_.close();
  }

  context_.allocator_->Release(buffer_->data());
  delete buffer_;
}

void BackendConn::Close() {
  if (aborted_) {
    return;
  }
  socket_.close();
  aborted_ = true;
}

void BackendConn::Abort(ErrorCode ec) {
  if (aborted_) {
    return;
  }
  LOG_ERROR << "BackendConn Abort, ec=" << ErrorCodeString(ec);
  aborted_ = true;
  no_recycle_  = true;
  is_reading_reply_ = false;

  socket_.close();
  write_timer_.cancel();
  read_timer_.cancel();
  write_timer_canceled_ = true;
  read_timer_canceled_ = true;
}

void BackendConn::SetReplyData(const char* data, size_t bytes) {
  // assert(is_reading_reply_ == false);
  buffer()->Reset();
  buffer()->push_reply_data(data, bytes);
}

void BackendConn::Reset() {
  assert(no_recycle_ == false);
  is_reading_reply_ = false;
  has_read_some_reply_ = false;
  reply_recv_complete_  = false;

  write_timer_canceled_ = false;
  read_timer_canceled_ = false;
  buffer()->Reset();
}

void BackendConn::ReadReply() {
  is_reading_reply_ = true;
  buffer_->inc_recycle_lock();
  read_timer_canceled_ = false;
  UpdateTimer(read_timer_, ErrorCode::E_BACKEND_READ_TIMEOUT);

  socket_.async_read_some(
      boost::asio::buffer(buffer_->free_space_begin(),
          buffer_->free_space_size()),
      std::bind(&BackendConn::HandleRead, shared_from_this(),
          std::placeholders::_1, std::placeholders::_2));
}

void BackendConn::TryReadMoreReply() {
  if (reply_recv_complete_ || is_reading_reply_ ||
      !buffer_->has_much_free_space()) {
    return;
  }
  ReadReply();
}

void BackendConn::WriteQuery(const char* data, size_t bytes) {
  if (aborted_) {
    std::weak_ptr<BackendConn> wptr(shared_from_this());
    context_.io_service_.post([wptr]() {
      if (auto ptr = wptr.lock()) {
        ptr->query_sent_callback_(ErrorCode::E_WRITE_QUERY);
      }
    });
    return;
  }
  if (!socket_.is_open()) {
    UpdateTimer(write_timer_, ErrorCode::E_BACKEND_CONNECT_TIMEOUT);
    write_timer_canceled_ = false;

    socket_.async_connect(remote_endpoint_, std::bind(&BackendConn::HandleConnect,
          shared_from_this(), data, bytes, std::placeholders::_1));
    return;
  }

  UpdateTimer(write_timer_, ErrorCode::E_BACKEND_WRITE_TIMEOUT);
  write_timer_canceled_ = false;
  boost::asio::async_write(socket_, boost::asio::buffer(data, bytes),
          std::bind(&BackendConn::HandleWrite, shared_from_this(), data, bytes,
              std::placeholders::_1, std::placeholders::_2));
}


void BackendConn::HandleWrite(const char * data, const size_t bytes,
    const boost::system::error_code& error, size_t bytes_transferred) {
  if (aborted_) {
    return;
  }
  write_timer_.cancel();
  write_timer_canceled_ = true;

  if (error) {
    LOG_INFO << "BackendConn::HandleWrite error, backend=" << this
             << " ep=" << remote_endpoint_ << " err=" << error.message();
    socket_.close();
    query_sent_callback_(ErrorCode::E_WRITE_QUERY);
    return;
  }

  g_stats_.bytes_to_backends_ += bytes_transferred;
  if (bytes_transferred < bytes) {
    LOG_DEBUG << "HandleWrite 向 backend 没写完, 继续写. backend=" << this;
    UpdateTimer(write_timer_, ErrorCode::E_BACKEND_WRITE_TIMEOUT);
    write_timer_canceled_ = false;
    boost::asio::async_write(socket_,
        boost::asio::buffer(data + bytes_transferred, bytes - bytes_transferred),
        std::bind(&BackendConn::HandleWrite, shared_from_this(),
                  data + bytes_transferred, bytes - bytes_transferred,
                  std::placeholders::_1, std::placeholders::_2));
  } else {
    LOG_DEBUG << "HandleWrite 向 backend 写完, 触发回调. backend=" << this;
    query_sent_callback_(ErrorCode::E_SUCCESS);
  }
}

void BackendConn::HandleRead(const boost::system::error_code& error,
                             size_t bytes_transferred) {
  if (aborted_) {
    return;
  }
  read_timer_.cancel();
  read_timer_canceled_ = true;

  is_reading_reply_ = false;
  if (error) {
    LOG_INFO << "HandleRead read error, backend=" << this
             << " ep=" << remote_endpoint_ << " err=" << error.message();
    socket_.close();
    reply_received_callback_(ErrorCode::E_READ_REPLY);
  } else {
    has_read_some_reply_ = true;
    g_stats_.bytes_from_backends_ += bytes_transferred;
    LOG_DEBUG << "HandleRead read ok, bytes_transferred="
              << bytes_transferred << " backend=" << this;

    buffer_->update_received_bytes(bytes_transferred);
    buffer_->dec_recycle_lock();

    reply_received_callback_(ErrorCode::E_SUCCESS);
  }
}

void BackendConn::HandleConnect(const char * data, size_t bytes,
                                const boost::system::error_code& connect_ec) {
  if (aborted_) {
    return;
  }
  write_timer_.cancel();
  write_timer_canceled_ = true;
  boost::system::error_code option_ec;
  if (!connect_ec) {
    boost::asio::ip::tcp::no_delay no_delay(true);
    socket_.set_option(no_delay, option_ec);

    // boost::asio::socket_base::send_buffer_size option(8192);
    boost::asio::socket_base::send_buffer_size option(16384);
    // boost::asio::socket_base::send_buffer_size option(32768);
    socket_.set_option(option, option_ec);

    if (!option_ec) {
      boost::asio::socket_base::keep_alive keep_alive(true);
      socket_.set_option(keep_alive, option_ec);
    }

    if (!option_ec) {
      boost::asio::socket_base::linger linger(true, 0);
      socket_.set_option(linger, option_ec);
    }
  }

  if (connect_ec || option_ec) {
    socket_.close();
    LOG_DEBUG << "HandleConnect error, err="
             << (connect_ec ? connect_ec.message() : option_ec.message())
             << " endpoint=" << remote_endpoint_
             << " backend=" << this;
    ++g_stats_.backend_connect_errors_;
    query_sent_callback_(ErrorCode::E_CONNECT);
    return;
  }

  UpdateTimer(write_timer_, ErrorCode::E_BACKEND_WRITE_TIMEOUT);
  write_timer_canceled_ = false;
  async_write(socket_, boost::asio::buffer(data, bytes),
      std::bind(&BackendConn::HandleWrite, shared_from_this(), data, bytes,
          std::placeholders::_1, std::placeholders::_2));
}


// TODO : connection_base
void BackendConn::OnTimeout(const boost::system::error_code& ec, ErrorCode timeout_code) {
  if (aborted_) {
    return;
  }

  if (ec == boost::asio::error::operation_aborted) {
    // timer was cancelled, take no action.
    return;
  }

  LOG_WARN << "BackendConn " << this << " timeout. timeout_code=" << ErrorCodeString(timeout_code)
           << " endpoint=" << remote_endpoint_
           << " read_timer_canceled_=" << read_timer_canceled_
           << " write_timer_canceled_=" << write_timer_canceled_
           << " backend=" << this;

  switch(timeout_code) {
  case ErrorCode::E_BACKEND_CONNECT_TIMEOUT:
    if (!write_timer_canceled_) {
      ++g_stats_.backend_connect_timeouts_;
      query_sent_callback_(timeout_code);
      Abort(timeout_code);
    }
    break;
  case ErrorCode::E_BACKEND_WRITE_TIMEOUT:
    if (!write_timer_canceled_) {
      ++g_stats_.backend_write_timeouts_;
      query_sent_callback_(timeout_code);
      Abort(timeout_code);
    }
    break;
  case ErrorCode::E_BACKEND_READ_TIMEOUT:
    if (!read_timer_canceled_) {
      ++g_stats_.backend_read_timeouts_;
      reply_received_callback_(timeout_code);
      Abort(timeout_code);
    }
    break;
  default:
    assert(false);
    break;
  }
}

void BackendConn::UpdateTimer(boost::asio::steady_timer& timer, ErrorCode timeout_code) {
  // TODO : 细致的超时处理, 包括connect/read/write/command
  int timeout = Config::Instance().socket_rw_timeout();
  size_t canceled = timer.expires_after(std::chrono::milliseconds(timeout));
  LOG_DEBUG << "BackendConn UpdateTimer timeout=" << timeout
           << " backend=" << this
           << " timeout_code=" << ErrorCodeString(timeout_code)
           << " canceled=" << canceled;

  std::weak_ptr<BackendConn> wptr(shared_from_this());
  timer.async_wait([wptr, timeout_code](const boost::system::error_code& ec) {
        if (auto ptr = wptr.lock()) {
          ptr->OnTimeout(ec, timeout_code);
        }
      });
}

}

