#include "redis_conn.h"

#include <iomanip>
#include <boost/bind.hpp>

#include <sys/socket.h>

#include "base/logging.h"
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
  , read_buf_begin_(0)
  , read_buf_end_(0)
  , write_buf_begin_(0)
  , write_buf_end_(0)
  , is_writing_(false)
  , status_(ST_UNINIT)
  , phase_(0)
  , timer_(io_service, boost::posix_time::seconds(3))
  , upstream_endpoint_(boost::asio::ip::address::from_string(host), port)
{
}

RedisConnection::~RedisConnection() {
}

void RedisConnection::SetSocketOptions() {
  boost::asio::ip::tcp::no_delay nodelay(true);
  socket_.set_option(nodelay);
  
  boost::asio::socket_base::linger linger(true, 0);
  socket_.set_option(linger);

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
    status_ = ST_CLOSED;
    return;
  }

  SetSocketOptions();

  //////////

  // const char MSET_PREFIX[] = "*3\r\n$4\r\nmget\r\n$4\r\nkeyx\r\n$4\r\nkey1\r\n";
  const char MSET_PREFIX[] = "*3\r\n$4\r\nmget\r\n$4\r\nkeyx\r\n$4\r\nkeyy\r\n";
  LOG_INFO << "RedisConnection::OnConnected conn=" << this << " upstream=" << upstream_endpoint_
           << ", write MSET_PREFIX=[" << MSET_PREFIX << "] size=" << sizeof(MSET_PREFIX) - 1;
  Write(MSET_PREFIX, sizeof(MSET_PREFIX) - 1);
  AsyncRead(); // 等待auth结果
}


// TODO : 命名统一，一律使用OnXXX, 不使用HandleXXX
void RedisConnection::OnKeepaliveTimer(const boost::system::error_code& error) {
  if (error) {
    Close();
    return;
  }

  if (status_ == ST_CLOSED) {
    LOG_DEBUG << "RedisConnection OnKeepaliveTimer, conn=" << this << " status=ST_CLOSED"
              << " upstream=" << upstream_endpoint_;
    return;
  } else if (status_ == ST_SUBSCRIBE_OK) {
    LOG_DEBUG << "RedisConnection OnKeepaliveTimer, conn=" << this << " status=ST_SUBSCRIBE_OK"
              << " upstream=" << upstream_endpoint_;
  } else {
    // TODO : 其他情况的timer
    LOG_DEBUG << "RedisConnection OnKeepaliveTimer, conn=" << this << " status=" << status_
              << " upstream=" << upstream_endpoint_;
  }

  timer_.expires_from_now(boost::posix_time::seconds(PING_INTERVAL));
  timer_.async_wait(boost::bind(&RedisConnection::OnKeepaliveTimer, shared_from_this(),
                     boost::asio::placeholders::error));
}

void RedisConnection::Close() {
  LOG_INFO << "Close conn=" << this;
  if (status_ == ST_CLOSED) {
    return;
  }

  status_ = ST_CLOSED;
  timer_.cancel();
  socket_.close();
}

void RedisConnection::AsyncRead() {
  if (read_buf_begin_ == 0) {
    // LOG_DEBUG("AsyncRead from begin. read_buf_end_=" << read_buf_end_);
  }
  socket_.async_read_some(boost::asio::buffer(read_buf_ + read_buf_end_, kReadBufLength - read_buf_end_),
      std::bind(&RedisConnection::HandleRead, shared_from_this(),
      std::placeholders::_1, // 占位符
      std::placeholders::_2));
}

void RedisConnection::AsyncWrite() {
  if (socket_.is_open()) {
    boost::asio::async_write(socket_,
        boost::asio::buffer(write_buf_ + write_buf_begin_, write_buf_end_ - write_buf_begin_),
        std::bind(&RedisConnection::HandleWrite, shared_from_this(),
            std::placeholders::_1, std::placeholders::_2));
  } else {
    LOG_WARN << "AsyncWrite try to write on bad socket,conn=" << this;
  }
}

void RedisConnection::Write(const char* data, size_t bytes) {
  if (bytes > 1024 * 32) {
    LOG_WARN << "Write too big packet size " << bytes << ", packet dropped";
    return;
  }

  if (extended_write_buf_.size() > 1024 * 512) {
    LOG_WARN << "Write too much data in write buf, new data dropped";
    return;
  }

  size_t memcpy_len = std::min(kWriteBufLength - write_buf_end_, bytes);
  if (memcpy_len) {
    memcpy(write_buf_ + write_buf_end_, data, memcpy_len);
  }
  if (memcpy_len < bytes) {
    extended_write_buf_.append((const char*)(data + memcpy_len), bytes - memcpy_len);
  }

  write_buf_end_ += memcpy_len;
  if (!is_writing_) {
    LOG_WARN << "Write begin";
    is_writing_ = true;
    AsyncWrite();
  } else {
    LOG_WARN << "Write do nothing";
  }
}

void RedisConnection::HandleWrite(const boost::system::error_code& error,
    size_t bytes_transferred) {
  if (error) {
    if (error != boost::asio::error::eof) {
      LOG_INFO << "HandleWrite err, conn=" << this << " err=" << error << "/" << error.message();
    } else {
      LOG_DEBUG << "HandleWrite closed by peer, conn=" << this;
    }
    Close();
    return;
  }

  is_writing_ = false;

  write_buf_begin_ += bytes_transferred;
  if (write_buf_begin_ < write_buf_end_) {
    AsyncWrite();
  } else {
    if (extended_write_buf_.empty()) {
      write_buf_begin_ = 0;
      write_buf_end_ = 0;
      is_writing_ = false;

      boost::system::error_code ec;
      socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
      if (ec) {
        LOG_INFO << "HandleWrite shutdown_send error, conn=" << this;
      } else {
        LOG_DEBUG << "HandleWrite shutdown_send ok, conn=" << this;
      }
    } else {
      write_buf_begin_ = 0;
      write_buf_end_ = std::min(extended_write_buf_.size(), (size_t)kWriteBufLength);
      // TODO : less copy
      memcpy(write_buf_, extended_write_buf_.c_str(), write_buf_end_);
      extended_write_buf_ = extended_write_buf_.substr(write_buf_end_);
      AsyncWrite();
    }
  }
}

void RedisConnection::Subscribe(const std::string& topic) {
  subscribed_topics_.insert(topic);
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


