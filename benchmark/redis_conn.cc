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

  // const uint8_t MSET_PREFIX[] = "*3\r\n$4\r\nmset\r\n";
  const uint8_t MSET_PREFIX[] = "get key1 key2 key3 key4 key5 key6 key7 key88\r\nget key1 key2 key3 key4 key5 key6 key7 key98\r\nget key1 key2 key3 key4 key5 key6 key7 key97\r\nget key1 key2 key3 key4 key5 key6 key7 key92\r\nget key1 key2 key3 key4 key5 key6 key7 key58\r\nget key1 key2 key3 key4 key5 key6 key7 key48\r\nget key1 key2 key3 key4 key5 key6 key7 key68\r\nget key1 key2 key3 key4 key5 key6 key7 key78\r\nget key1 key2 key3 key4 key5 key6 key7 key77\r\nget key1 key2 key3 key4 key5 key6 key7 key66\r\nget key1 key2 key3 key4 key5 key6 key7 key49\r\nget key1 key2 key3 key4 key5 key6 key7 key61\r\nget key1 key2 key3 key4 key5 key6 key7 key31\r\nget key1 key2 key3 key4 key5 key6 key7 key25\r\nget key1 key2 key3 key4 key5 key6 key7 key18\r\nget key1 key2 key3 key4 key5 key6 key7 key91\r\nget key1 key2 key3 key4 key5 key6 key7 key108\r\nget key1 key2 key3 key4 key5 key6 key7 key69\r\nget key1 key2 key3 key4 key5 key6 key7 key93\r\nget key1 key2 key3 key4 key5 key6 key7 key128\r\n";
  LOG_INFO << "RedisConnection::OnConnected conn=" << this << " upstream=" << upstream_endpoint_
           << ", write MSET_PREFIX=[" << MSET_PREFIX << "] size=" << sizeof(MSET_PREFIX) - 1;

  // TODO : do sth
  Write(MSET_PREFIX, sizeof(MSET_PREFIX) - 1);
  // AsyncRead(); // 等待auth结果
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
    // TODO : do sth
  //PingReqOutPacket packet;
  //Write((uint8_t*)packet.data(), packet.size());
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

void RedisConnection::Write(const uint8_t* data, size_t bytes) {
  // TODO : 过载保护细化, 例如丢弃老行情数据，用新行情替换之
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
  is_writing_ = false;
  write_buf_begin_ = 0;
  write_buf_end_ = 0;

  if (error) {
    LOG_WARN << "HandleWrite error : " << error << " " << error.message();
    Close();
    return;
  }
  if (false && phase_ == 0) {
    phase_ = 1;
    const uint8_t MSET_BODY[] = "$4\r\nkey1\r\n$6\r\nvalue1\r\n";
    LOG_DEBUG << "HandleWrite ok, to write MSET_BODY, conn=" << this << " written_bytes=" << bytes_transferred;
    Write(MSET_BODY, sizeof(MSET_BODY) - 1);
  } else if (true || phase_ == 1) {
    phase_ = 2;
    AsyncRead();
    LOG_DEBUG << "HandleWrite ok, to read reply, conn=" << this << " written_bytes=" << bytes_transferred;
  } else {
    assert(false);
  }
  return;

  write_buf_begin_ += bytes_transferred;
  if (write_buf_begin_ < write_buf_end_) {
    AsyncWrite();
  } else {
    if (extended_write_buf_.empty()) {
      write_buf_begin_ = 0;
      write_buf_end_ = 0;
      is_writing_ = false;
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

std::string DumpBytes(const uint8_t* data, size_t size) {
  return std::string();

  std::ostringstream oss;
  for(size_t i = 0; i < size; ++i) {
    if (i % 8 == 0) {
      oss << " /" << std::setw(3) << std::setfill('0') << (i*8) << ":";
    }
    uint16_t v = data[i];
    oss << " " << std::setw(2) << std::setfill('0') << std::hex << v; 
  }
  return oss.str();
}

int RedisConnection::ProcessPacket() {
  /*
  // TODO : buffer循环
  if (read_buf_end_ <= read_buf_begin_
     || read_buf_end_ - read_buf_begin_ < MIN_HEADER_BYTES) {
    return 0;
  }
  uint8_t* fixed_header = read_buf_ + read_buf_begin_;

#if 1
  int remaining_len_bytes = 0;
  int remaining_len = UnmarshalRemainingLength(fixed_header + 1, read_buf_end_ - read_buf_begin_,
                                            &remaining_len_bytes);

  if ((remaining_len > 1024 * 512) && (read_buf_begin_ + 6 < read_buf_end_)) {
  //LOG_WARN("MqttInPacket Parse large remaining_len=" << remaining_len
  //         << ", remaining_len_bytes=" << remaining_len_bytes
  //         << ", data[0]=" << std::hex << (uint16_t)fixed_header[0]
  //         << ", data[1]=" << std::hex << (uint16_t)fixed_header[1]
  //         << ", data[2]=" << std::hex << (uint16_t)fixed_header[2]
  //         << ", data[3]=" << std::hex << (uint16_t)fixed_header[3]
  //         << ", data[4]=" << std::hex << (uint16_t)fixed_header[4]
  //         << ", data[5]=" << std::hex << (uint16_t)fixed_header[5]
  //         );
  }
  
  if (remaining_len == -1) {
    return 0;
  }
  if (remaining_len < 0) {
    // LOG_WARN("MqttInPacket UnmarshalRemainingLength error, conn=" << this);
    return -1;
  }
  size_t fixed_header_len = 1 + remaining_len_bytes; 
#else
  uint16_t remaining_len = fixed_header[1];
  size_t fixed_header_len = 2; 
  if (fixed_header[1] & 0x80) {
    if (read_buf_end_ - read_buf_begin_ <= MIN_HEADER_BYTES + 1) {
      return 0;
    }
    remaining_len = ((uint16_t)fixed_header[2] << 7) + (fixed_header[1] & 0x7F);
    //TODO : 更长的情况, 参考协议文档2.2.3 Remaining Length 一节
    ++fixed_header_len; 
  }
#endif 

  if (remaining_len > 4 * 1024 && (read_buf_begin_ + 10 < read_buf_end_)) {
  //LOG_DEBUG("MqttInPacket large packet, read_buf_begin_=" << read_buf_begin_
  //      << " read_buf_end_=" << read_buf_end_ << " remaining_len=" << remaining_len
  //      << " fixed_header_len=" << fixed_header_len << " conn=" << this);
    std::ostringstream oss;
    for(size_t i = 0; i < 10 && (read_buf_begin_ + i < read_buf_end_); ++i) {
      uint16_t v = read_buf_[read_buf_begin_ + i];
      oss << " " << std::setw(2) << std::setfill('0') << std::hex << v; 
    }
    // LOG_WARN("MqttInPacket large packet dump :" << oss.str());
  }

//LOG_DEBUG("MqttInPacket dump all data, read_buf_begin_=" << read_buf_begin_
//          << " read_buf_end_=" << read_buf_end_ << " remaining_len=" << remaining_len
//          << " fixed_header_len=" << fixed_header_len << " conn=" << this
//          << " dump=" << DumpBytes(read_buf_ + read_buf_begin_, size_t(read_buf_end_ - read_buf_begin_)));
  if (read_buf_begin_ + fixed_header_len + remaining_len > read_buf_end_) {
    // LOG_DEBUG("MqttInPacket incomplete packet, read_buf_begin_=" << read_buf_begin_);
    return 0;
  }
  
  MqttInPacketPtr packet = CreatePacket(shared_from_this(), fixed_header);
  if (packet) {
    int ret = packet->Parse(fixed_header, fixed_header_len, remaining_len);
    if (ret != 0) {
    //LOG_WARN("MqttInPacket Parse error, read_buf_begin_=" << read_buf_begin_
    //    << " read_buf_end_=" << read_buf_end_ << " remaining_len=" << remaining_len
    //    << " fixed_header_len=" << fixed_header_len
    //    << " conn=" << this << std::hex << (uint16_t)fixed_header[0]);
      return -2;
    } else {
    //LOG_DEBUG("MqttInPacket Parse ok, read_buf_begin_=" << read_buf_begin_
    //    << " read_buf_end_=" << read_buf_end_ << " remaining_len=" << remaining_len
    //    << " fixed_header_len=" << fixed_header_len << " conn=" << this);
      packet->Response();
    }
  } else {
    // 未知或非法packet会丢弃掉
    // LOG_WARN("unknown packet type, bytes[0]=0x" << std::hex << (uint16_t)fixed_header[0]);
    return -3; // 关闭连接
  }

  return fixed_header_len + remaining_len;
  */
    return 0;
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
  LOG_DEBUG << "HandleRead ok, conn=" << this << " bytes=" << bytes_transferred
            << " data=[" << std::string((char*)read_buf_ + read_buf_end_, bytes_transferred) << "]";

  read_buf_begin_ = read_buf_end_ = 0;
  AsyncRead();
  return;

  read_buf_end_ += bytes_transferred;

  while(true) {
    int bytes = ProcessPacket();
    if (bytes == 0) {
    //LOG_DEBUG("only part of bytes in read buf handled, read_buf_begin_=" << read_buf_begin_
    //          << " read_buf_end_=" << read_buf_end_ << " left=" << read_buf_end_ - read_buf_begin_);
      memmove(read_buf_, read_buf_ + read_buf_begin_, read_buf_end_ - read_buf_begin_);
      read_buf_end_ -= read_buf_begin_;
      read_buf_begin_ = 0;
      break;
    } else if (bytes < 0) {
      // LOG_DEBUG("read buf data process error");
      Close();
      break;
    }
  
    read_buf_begin_ += bytes;
    if (read_buf_begin_ == read_buf_end_) {
      read_buf_begin_ = 0;
      read_buf_end_ = 0;
      // LOG_DEBUG("all bytes in read buf handled");
      break;
    } else if (read_buf_begin_ > read_buf_end_) {
      // LOG_WARN("ProcessPacket bytes offset error, last_packet_size=" << bytes << " begin=" << read_buf_begin_ << " end=" << read_buf_end_);
      Close();
      break;
    }
  }

  AsyncRead(); // 即使数据读完，也要继续读, 以检测网络错误
}

}

