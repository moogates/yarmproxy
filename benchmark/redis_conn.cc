#include "redis_conn.h"

#include <iomanip>
#include <iostream>

#include <sys/socket.h>

#include "../proxy/logging.h"
#include "../proxy/redis_protocol.h"

namespace yarmproxy {

int PING_INTERVAL    = 60;

std::shared_ptr<RedisConnection> RedisConnection::Create(boost::asio::io_service& io_service,
                                  const std::string& host,
                                  short port) {
  std::shared_ptr<RedisConnection> conn(new RedisConnection(io_service, host, port));
  conn->Initialize();
  return conn;;
}

RedisConnection::RedisConnection(boost::asio::io_service& io_service,
                                  const std::string& host,
                                  short port)
  : io_service_(io_service)
  , socket_(io_service)
  , timer_(io_service, boost::posix_time::seconds(3))
  , upstream_endpoint_(boost::asio::ip::address::from_string(host), port)
{
  query_data_ = "*3\r\n$4\r\nmget\r\n$4\r\nkey1\r\n$4\r\nkey2\r\n";
}

RedisConnection::~RedisConnection() {
}

void RedisConnection::SetSocketOptions() {
  boost::asio::ip::tcp::no_delay nodelay(true);
  socket_.set_option(nodelay);
  
  boost::asio::socket_base::keep_alive keep_alive(true);
  socket_.set_option(keep_alive);

  // TODO : mac 下无法编译
#ifdef LINUX
  int fd = socket_.native_handle();
  int optval = 5;
  setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &optval, sizeof(optval));
  optval = 20;
  setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &optval, sizeof(optval));
  optval = 5;
  setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &optval, sizeof(optval));
#endif
}

void RedisConnection::Initialize() {
  socket_.async_connect(upstream_endpoint_,
        std::bind(&RedisConnection::OnConnected,
          shared_from_this(),
          std::placeholders::_1));

  timer_.expires_from_now(boost::posix_time::seconds(PING_INTERVAL));
  timer_.async_wait(std::bind(&RedisConnection::OnKeepaliveTimer,
                    shared_from_this(),
                    std::placeholders::_1));

  LOG_DEBUG << "RedisConnection Initialized ok, conn=" << this
            << " upstream=" << upstream_endpoint_;
}

void RedisConnection::OnConnected(const boost::system::error_code& error) {
  if (error) {
    LOG_WARN << "RedisConnection OnConnected error=" << error.message()
             << " upstream=" << upstream_endpoint_;
    socket_.close();
    closed_ = true;
    return;
  }

  SetSocketOptions();

  AsyncWrite();
  AsyncRead();
}

// TODO : 命名统一，一律使用OnXXX, 不使用HandleXXX
void RedisConnection::OnKeepaliveTimer(const boost::system::error_code& error) {
  if (error) {
    LOG_DEBUG << "RedisConnection timer error, conn=" << this;
    Close();
    return;
  }

  if (closed_) {
    LOG_DEBUG << "RedisConnection OnKeepaliveTimer closed, conn=" << this
              << " upstream=" << upstream_endpoint_;
    return;
  }
  LOG_DEBUG << "RedisConnection OnKeepaliveTimer ok, conn=" << this
            << " upstream=" << upstream_endpoint_;

  timer_.expires_from_now(boost::posix_time::seconds(PING_INTERVAL));
  timer_.async_wait(std::bind(&RedisConnection::OnKeepaliveTimer, shared_from_this(),
                     std::placeholders::_1));
}

void RedisConnection::Close() {
  LOG_INFO << "Close conn=" << this;
  if (closed_) {
    return;
  }

  closed_ = true;
  timer_.cancel();
  socket_.close();
}

void RedisConnection::AsyncRead() {
  socket_.async_read_some(boost::asio::buffer(read_buf_, kReadBufLength),
      std::bind(&RedisConnection::HandleRead, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
}

void RedisConnection::AsyncWrite() {
  if (is_writing_) {
    return;
  }
  is_writing_ = true;
  if (socket_.is_open()) {
    boost::asio::async_write(socket_, boost::asio::buffer(query_data_.data() +
            query_written_bytes_, query_data_.size() - query_written_bytes_),
        std::bind(&RedisConnection::HandleWrite, shared_from_this(),
            std::placeholders::_1, std::placeholders::_2));
  } else {
    LOG_WARN << "AsyncWrite try to write on bad socket,conn=" << this;
  }
}

void RedisConnection::HandleWrite(const boost::system::error_code& error,
    size_t bytes_transferred) {
  if (error) {
    if (error != boost::asio::error::eof) {
      LOG_INFO << "HandleWrite err, conn=" << this << " err=" << error << "/" << error.message();
    } else {
      LOG_DEBUG << "HandleWrite EOF, conn=" << this;
    }
    Close();
    return;
  }

  is_writing_ = false;

  query_written_bytes_ += bytes_transferred;
  if (query_written_bytes_ < query_data_.size()) {
    AsyncWrite();
  } else {
    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
    if (ec) {
      LOG_INFO << "HandleWrite shutdown_send error, conn=" << this;
    } else {
      LOG_DEBUG << "HandleWrite shutdown_send ok, conn=" << this;
    }
  }
}

// TODO : 在合理的时间内没有收到CONNECT报文，服务端应该关闭这个连接
void RedisConnection::HandleRead(const boost::system::error_code& error, size_t bytes_transferred) {
  if (error) {
    if (error != boost::asio::error::eof) {
      LOG_INFO << "HandleRead err, conn=" << this << " err=" << error << "/" << error.message();
    } else {
      LOG_DEBUG << "HandleRead closed by peer, conn=" << this;
    }
    Close();
    return;
  }

  std::cout.write(reinterpret_cast<const char*>(read_buf_), bytes_transferred);
  AsyncRead();
}

}


