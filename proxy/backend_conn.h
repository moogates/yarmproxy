#ifndef _YARMPROXY_BACKEND_CONN_H_
#define _YARMPROXY_BACKEND_CONN_H_

#include <functional>
#include <memory>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

#include "read_buffer.h"

namespace yarmproxy {
using Endpoint = boost::asio::ip::tcp::endpoint;

class WorkerContext;
class ReadBuffer;

enum class ErrorCode;

typedef std::function<void(ErrorCode ec)> BackendReplyReceivedCallback;
typedef std::function<void(ErrorCode ec)> BackendQuerySentCallback;

class BackendConn : public std::enable_shared_from_this<BackendConn> {
public:
  BackendConn(WorkerContext& context, const Endpoint& endpoint);
  ~BackendConn();

  void WriteQuery(const char* data, size_t bytes);

  void ReadReply();
  void TryReadMoreReply();

  void SetReplyData(const char* data, size_t bytes);

  void SetReadWriteCallback(const BackendQuerySentCallback& query_sent_callback,
                            const BackendReplyReceivedCallback& reply_received_callback) {
    query_sent_callback_ = query_sent_callback;
    reply_received_callback_ = reply_received_callback;
  }
private:
  void HandleWrite(const char * buf, const size_t bytes,
      const boost::system::error_code& error, size_t bytes_transferred);
  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);
  void HandleConnect(const char * buf, size_t bytes, const boost::system::error_code& error);
public:
  void Close();
  void Reset();
  ReadBuffer* buffer() {
    return buffer_;
  }

  bool finished() const {
    return reply_recv_complete_ && buffer_->unprocessed_bytes() == 0;
  }

  const Endpoint& remote_endpoint() const {
    return remote_endpoint_;
  }
  void set_reply_recv_complete() {
    reply_recv_complete_ = true;
  }
  void set_no_recycle() {
    no_recycle_ = true;
  }
  bool reply_recv_complete() const {
    return reply_recv_complete_;
  }
  bool recyclable() const {
    return !no_recycle_ && finished();
  }
  bool has_read_some_reply() const {
    return has_read_some_reply_;
  }
private:
  WorkerContext& context_;
  ReadBuffer* buffer_;
  Endpoint remote_endpoint_;
  boost::asio::ip::tcp::socket socket_;

  BackendReplyReceivedCallback reply_received_callback_;
  BackendQuerySentCallback query_sent_callback_;

  bool is_reading_reply_    = false; // TODO : merge into a flag
  bool has_read_some_reply_ = false;
  bool reply_recv_complete_ = false;
  bool no_recycle_          = false;

  bool closed_ = false;

  boost::asio::steady_timer timer_;
  int timer_ref_count_ = 0;

  void UpdateTimer();
  void RevokeTimer();
  void OnTimeout(const boost::system::error_code& error);
};

}

#endif // _YARMPROXY_BACKEND_CONN_H_
