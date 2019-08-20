#ifndef _UPSTREAM_CONN_H_
#define _UPSTREAM_CONN_H_

#include <map>
#include <queue>
#include <functional>

#include <boost/asio.hpp>
#include "base/logging.h"

#include "read_buffer.h"

using namespace boost::asio;

namespace mcproxy {

typedef std::function<void(const boost::system::error_code& error)> UpstreamReadCallback;
typedef std::function<void(const boost::system::error_code& error)> UpstreamWriteCallback;

class BackendConn {
public:
  BackendConn(boost::asio::io_service& io_service, 
      const ip::tcp::endpoint& upendpoint);
  ~BackendConn();

  void ForwardRequest(const char* data, size_t bytes, bool has_more_data);

  void ReadResponse();
  void TryReadMoreData();

  ip::tcp::socket& socket() {
    return socket_;
  }

  void SetReadWriteCallback(const UpstreamReadCallback& read_callback, const UpstreamWriteCallback& write_callback) {
    backend_read_callback_ = read_callback;
    uptream_write_callback_ = write_callback;
  }
private:
  void HandleWrite(const char * buf, const size_t bytes, bool has_more_data,
      const boost::system::error_code& error, size_t bytes_transferred);
  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);
  void HandleConnect(const char * buf, size_t bytes, bool has_more_data, const boost::system::error_code& error);
public:
  ReadBuffer read_buffer_;
private:
  ip::tcp::endpoint backend_endpoint_;
  ip::tcp::socket socket_;
  UpstreamReadCallback backend_read_callback_;
  UpstreamWriteCallback uptream_write_callback_;

  bool is_reading_more_;
};

class BackendConnPool {
private:
  boost::asio::io_service& io_service_;
  std::map<ip::tcp::endpoint, std::queue<BackendConn*>> conn_map_;
  std::map<BackendConn*, ip::tcp::endpoint> active_conns_;
public:
  BackendConnPool(boost::asio::io_service& asio_service) : io_service_(asio_service) {
  }

  BackendConn * Allocate(const ip::tcp::endpoint & ep);
  void Release(BackendConn * conn);
};

}

#endif // _UPSTREAM_CONN_H_
