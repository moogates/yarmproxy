#ifndef _BACKEND_CONN_H_
#define _BACKEND_CONN_H_

#include <map>
#include <queue>
#include <functional>

#include <boost/asio.hpp>
#include "base/logging.h"

#include "worker_pool.h"
#include "read_buffer.h"

using namespace boost::asio;

namespace mcproxy {

typedef std::function<void(const boost::system::error_code& error)> BackendReplyReceivedCallback;
typedef std::function<void(const boost::system::error_code& error)> BackendRequestSentCallback;

class BackendConn {
public:
  BackendConn(WorkerContext& context, const ip::tcp::endpoint& upendpoint);
  ~BackendConn();

  void ForwardRequest(const char* data, size_t bytes, bool has_more_data);

  void ReadResponse();
  void TryReadMoreData();

  ip::tcp::socket& socket() {
    return socket_;
  }
  ip::tcp::endpoint& remote_endpoint() {
    return remote_endpoint_;
  }

  void SetReadWriteCallback(const BackendRequestSentCallback& request_sent_callback, const BackendReplyReceivedCallback& response_received_callback) {
    request_sent_callback_ = request_sent_callback;
    response_received_callback_ = response_received_callback;
  }
private:
  void HandleWrite(const char * buf, const size_t bytes, bool has_more_data,
      const boost::system::error_code& error, size_t bytes_transferred);
  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);
  void HandleConnect(const char * buf, size_t bytes, bool has_more_data, const boost::system::error_code& error);
public:
  ReadBuffer* buffer() {
    return read_buffer_;
  }
private:
  WorkerContext& context_;
  ReadBuffer* read_buffer_;
  ip::tcp::endpoint remote_endpoint_;
  ip::tcp::socket socket_;
  BackendReplyReceivedCallback response_received_callback_;
  BackendRequestSentCallback request_sent_callback_;

  bool is_reading_more_;
};

class BackendConnPool {
private:
  WorkerContext& context_;
  std::map<ip::tcp::endpoint, std::queue<BackendConn*>> conn_map_;
  std::map<BackendConn*, ip::tcp::endpoint> active_conns_;
public:
  BackendConnPool(WorkerContext& context) : context_(context) {
  }

  BackendConn * Allocate(const ip::tcp::endpoint & ep);
  void Release(BackendConn * conn);
};

}

#endif // _BACKEND_CONN_H_
