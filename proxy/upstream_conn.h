#ifndef _UPSTREAM_CONN_H_
#define _UPSTREAM_CONN_H_

#include <map>
#include <string>
#include <functional>

#include <boost/asio.hpp>
#include "base/logging.h"

using namespace boost::asio;

namespace mcproxy {

typedef std::function<void(const char* data, size_t bytes, const boost::system::error_code& error)> UpstreamCallback;

class UpstreamConn {
public:
  UpstreamConn(boost::asio::io_service& io_service, 
      const ip::tcp::endpoint& upendpoint,
      const UpstreamCallback& uptream_callback);

  void ForwardRequest(const char* data, size_t bytes);

  void HandleWrite(const char * buf, const size_t bytes,
      const boost::system::error_code& error, size_t bytes_transferred);
  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);
  void HandleConnect(const char * buf, size_t bytes, const boost::system::error_code& error);

  void OnResponseProcessed(size_t bytes) {
    popped_bytes_ += bytes;
  }

  void Reset() {
    popped_bytes_ = pushed_bytes_ = 0;
  }

  ip::tcp::socket & socket() {
    return socket_;
  }

  void set_upstream_callback(const UpstreamCallback& cb) {
    uptream_cb_ = cb;
  }

  enum { BUFFER_SIZE = 64 * 1024};
  char buf_[BUFFER_SIZE];

  size_t popped_bytes_;
  size_t pushed_bytes_;
private:
  ip::tcp::endpoint upstream_endpoint_;
  ip::tcp::socket socket_;
  UpstreamCallback uptream_cb_;
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
