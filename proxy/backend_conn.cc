#include "backend_conn.h"

#include "base/logging.h"

#include "allocator.h"
#include "error_code.h"
#include "read_buffer.h"
#include "stats.h"
#include "worker_pool.h"

namespace yarmproxy {
// TODO: 更好的容错，以及错误返回信息, 例如
//   客户端格式错误时候，memcached返回错误信息: "CLIENT_ERROR bad data chunk\r\n"

BackendConn::BackendConn(WorkerContext& context,
    const Endpoint& endpoint)
  : context_(context)
  , buffer_(new ReadBuffer(context.allocator_->Alloc(), context.allocator_->slab_size()))
  , remote_endpoint_(endpoint)
  , socket_(context.io_service_) {
  ++g_stats_.backend_conns_;
  LOG_DEBUG << "BackendConn ctor, count=" << g_stats_.backend_conns_;
}

BackendConn::~BackendConn() {
  --g_stats_.backend_conns_;
  LOG_DEBUG << "BackendConn " << this << " dtor, count=" << g_stats_.backend_conns_;
  socket_.close();

  context_.allocator_->Release(buffer_->data());
  delete buffer_;
}

void BackendConn::Close() {
  LOG_DEBUG << "BackendConn Close, backend=" << this;
  socket_.close();
}

void BackendConn::SetReplyData(const char* data, size_t bytes) {
  assert(is_reading_reply_ == false);
  buffer()->Reset();
  buffer()->push_reply_data(data, bytes);
}

void BackendConn::Reset() {
  is_reading_reply_ = false;
  reply_recv_complete_  = false;
  buffer()->Reset();
}

void BackendConn::ReadReply() {
  is_reading_reply_ = true;
  buffer_->inc_recycle_lock();
  socket_.async_read_some(boost::asio::buffer(buffer_->free_space_begin(),
          buffer_->free_space_size()),
      std::bind(&BackendConn::HandleRead, shared_from_this(),
          std::placeholders::_1, std::placeholders::_2));
}

void BackendConn::TryReadMoreReply() {
  if (reply_recv_complete_ || is_reading_reply_
      || !buffer_->has_much_free_space()) {
    return;
  }
  ReadReply();
}

void BackendConn::WriteQuery(const char* data, size_t bytes) { // TODO : remove has_more_data param
  if (!socket_.is_open()) {
    socket_.async_connect(remote_endpoint_, std::bind(&BackendConn::HandleConnect,
          shared_from_this(), data, bytes, std::placeholders::_1));
    return;
  }

  boost::asio::async_write(socket_, boost::asio::buffer(data, bytes),
          std::bind(&BackendConn::HandleWrite, shared_from_this(), data, bytes,
              std::placeholders::_1, std::placeholders::_2));
}


void BackendConn::HandleWrite(const char * data, const size_t bytes,
    const boost::system::error_code& error, size_t bytes_transferred) {
  if (error) {
    LOG_WARN << "BackendConn::HandleWrite error, backend=" << this
             << " ep=" << remote_endpoint_ << " err=" << error.message();
    socket_.close();
    query_sent_callback_(ErrorCode::E_WRITE_QUERY);
    return;
  }

  g_stats_.bytes_to_backends_ += bytes_transferred;
  if (bytes_transferred < bytes) {
    LOG_DEBUG << "HandleWrite 向 backend 没写完, 继续写. backend=" << this;
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
  is_reading_reply_ = false;
  if (error) {
    LOG_WARN << "HandleRead read error, backend=" << this
             << " ep=" << remote_endpoint_ << " err=" << error.message();
    socket_.close();
    reply_received_callback_(ErrorCode::E_READ_REPLY);
  } else {
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
  boost::system::error_code option_ec;
  if (!connect_ec) {
    boost::asio::ip::tcp::no_delay no_delay(true);
    socket_.set_option(no_delay, option_ec);

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
    LOG_WARN << "HandleConnect error, connect_ec=" << connect_ec.message()
             << " option_ec=" << option_ec.message()
             << " endpoint=" << remote_endpoint_
             << " backend=" << this;
    query_sent_callback_(ErrorCode::E_CONNECT);
    return;
  }

  async_write(socket_, boost::asio::buffer(data, bytes),
      std::bind(&BackendConn::HandleWrite, shared_from_this(), data, bytes,
          std::placeholders::_1, std::placeholders::_2));
}

}

