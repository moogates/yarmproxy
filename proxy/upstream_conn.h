#ifndef _UPSTREAM_CONN_H_
#define _UPSTREAM_CONN_H_

#include <map>
#include <string>
#include <functional>

#include <boost/asio.hpp>
#include "base/logging.h"

using namespace boost::asio;

namespace mcproxy {

typedef std::function<void(const boost::system::error_code& error)> UpstreamReadCallback;
typedef std::function<void(size_t written_bytes, const boost::system::error_code& error)> UpstreamWriteCallback;

class UpstreamConn {
public:
  UpstreamConn(boost::asio::io_service& io_service, 
      const ip::tcp::endpoint& upendpoint,
      const UpstreamReadCallback& uptream_read_callback,
      const UpstreamWriteCallback& uptream_write_callback);
  ~UpstreamConn();

  void ForwardRequest(const char* data, size_t bytes, bool has_more_data);

  void ReadResponse();
  void TryReadMoreData();
private:
  void HandleWrite(const char * buf, const size_t bytes, bool has_more_data,
      const boost::system::error_code& error, size_t bytes_transferred);
  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);
  void HandleConnect(const char * buf, size_t bytes, bool has_more_data, const boost::system::error_code& error);
public:

  void OnResponseProcessed(size_t bytes) {
    popped_bytes_ += bytes;
  }

  void ResetBuffer() {
    popped_bytes_ = pushed_bytes_ = 0;
  }

  ip::tcp::socket & socket() {
    return socket_;
  }

  void set_upstream_read_callback(const UpstreamReadCallback& read_callback, const UpstreamWriteCallback& write_callback) {
    upstream_read_callback_ = read_callback;
    uptream_write_callback_ = write_callback;
  }

  const char* to_transfer_data() const { // 可以向下游传递的数据
    return buf_ + popped_bytes_;
  }
  size_t to_transfer_bytes() const {
    return std::min(pushed_bytes_, parsed_bytes_) - popped_bytes_;
    if (pushed_bytes_ > parsed_bytes_) {
      return parsed_bytes_ - popped_bytes_;
    } else {
      return pushed_bytes_ - popped_bytes_;
    }
  }
  void update_transfered_bytes(size_t transfered);

  void update_parsed_bytes(size_t bytes) {
    parsed_bytes_ += bytes;
  }
//size_t parsed_bytes() const {
//  return parsed_bytes_;
//}
  const char * unparsed_data() const {
    return buf_ + parsed_bytes_;
  }
  size_t unparsed_bytes() const;

  // enum { BUFFER_SIZE = 64 * 1024};
  enum { BUFFER_SIZE = 32 * 1024}; // TODO : use c++11 enum
  char buf_[BUFFER_SIZE];

  size_t popped_bytes_;
  size_t pushed_bytes_;

private:
  size_t parsed_bytes_;

  ip::tcp::endpoint upstream_endpoint_;
  ip::tcp::socket socket_;
  UpstreamReadCallback upstream_read_callback_;
  UpstreamWriteCallback uptream_write_callback_;

  bool is_reading_more_;
};

class UpstreamConnPool {
private:
  typedef std::map<ip::tcp::endpoint, std::vector<UpstreamConn*>> ConnMap;
  ConnMap conn_map_;
public:
  //static UpstreamConnPool & instance() {
  //  static UpstreamConnPool p; 
  //  return p;
  //}

  //std::mutex mutex_; // TODO : 可以是全局的，也可以是线程专有的

  UpstreamConn * Pop(const ip::tcp::endpoint & ep);
  void Push(const ip::tcp::endpoint & ep, UpstreamConn * conn);
};

}

#endif // _UPSTREAM_CONN_H_
