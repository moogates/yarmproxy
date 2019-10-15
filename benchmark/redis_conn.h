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

  void Subscribe(const std::string& topic);
  inline void refresh_keepalive() {
    // TODO : 优化keepalive管理
    keepalive_ = time(NULL);
  }

  void Write(const char* data, size_t len);

  boost::asio::io_service & io_service() {
    return io_service_;
  }
  bool IsClosed() const  { return status_ == ST_CLOSED; }

  virtual const std::string& topic() { static std::string emtpy; return emtpy; }
  virtual bool is_publisher() const  { return false; }
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
  enum Status {
    ST_UNINIT      = 1,
    ST_ACCEPTED    = 1,
    ST_CONNECTED   = 2,
    ST_CLOSED      = 3,
    ST_AUTHING     = 4,
    ST_SUBSCRIBING = 5,
    ST_SUBSCRIBE_OK = 6,
  };

  std::string query_data_;
  size_t query_sent_counter_ = 0;
  
  std::set<std::string> subscribed_topics_;
  
  enum { kReadBufLength = 64 * 1024 };
  char read_buf_[kReadBufLength];
  size_t read_buf_begin_, read_buf_end_;

  enum { kWriteBufLength = 4 * 1024 };
  char write_buf_[kWriteBufLength];
  size_t write_buf_begin_, write_buf_end_;
  std::string extended_write_buf_;
  bool is_writing_;

  Status status_;
  int phase_;

  boost::asio::deadline_timer timer_;
  boost::asio::ip::tcp::endpoint upstream_endpoint_;
};

}

#endif // _YARMCLIENT_REDIS_CONNECTION_
