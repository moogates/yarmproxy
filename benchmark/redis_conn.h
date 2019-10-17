#ifndef _YARMCLIENT_REDIS_CONNECTION_
#define _YARMCLIENT_REDIS_CONNECTION_

#include <set>
#include <string>
#include <memory>

#include <boost/asio.hpp>

// using namespace boost::asio;

namespace yarmproxy {

class RedisConnection;

class RedisConnection : public std::enable_shared_from_this<RedisConnection> {
public:
  static std::shared_ptr<RedisConnection> Create(boost::asio::io_service& io_service,
                                   const std::string& host,
                                   short port);
  virtual ~RedisConnection();

  boost::asio::ip::tcp::socket& socket() {
    return socket_;
  }
  
  void Initialize();
  void OnConnected(const boost::system::error_code& error);

  void AsyncRead();
  void AsyncWrite();
  void Close();

  inline void refresh_keepalive() {
    // TODO : 优化keepalive管理
    keepalive_ = time(NULL);
  }

  boost::asio::io_service & io_service() {
    return io_service_;
  }
  bool IsClosed() const  { return closed_; }

  virtual void OnKeepaliveTimer(const boost::system::error_code& error);
private:
  void SetSocketOptions();
  RedisConnection(boost::asio::io_service& io_service, 
                                  const std::string& host,
                                  short port);

  // HttpRequest * CreateRequest();
  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);
  void HandleWrite(const boost::system::error_code& /*error*/, size_t /*bytes_transferred*/);

  boost::asio::io_service & io_service_;
  boost::asio::ip::tcp::socket socket_;

  time_t keepalive_;

  // const std::string& query_data_;
  std::string query_data_;
  size_t query_written_bytes_ = 0;
  bool is_writing_ = false;
  
  enum { kReadBufLength = 64 * 1024 };
  char read_buf_[kReadBufLength];

  bool closed_ = false;

  boost::asio::deadline_timer timer_;
  boost::asio::ip::tcp::endpoint upstream_endpoint_;
};

}

#endif // _YARMCLIENT_REDIS_CONNECTION_
