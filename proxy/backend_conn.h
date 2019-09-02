#ifndef _YARMPROXY_BACKEND_CONN_H_
#define _YARMPROXY_BACKEND_CONN_H_

#include <functional>
#include <memory>

#include <boost/asio.hpp>
using namespace boost::asio;

namespace yarmproxy {
class WorkerContext;
class ReadBuffer;

enum class ErrorCode;

typedef std::function<void(ErrorCode ec)> BackendReplyReceivedCallback;
typedef std::function<void(ErrorCode ec)> BackendQuerySentCallback;

class BackendConn : public std::enable_shared_from_this<BackendConn> {
public:
  BackendConn(WorkerContext& context, const ip::tcp::endpoint& endpoint);
  ~BackendConn();

  void ForwardQuery(const char* data, size_t bytes, bool has_more_data);

  void ReadReply();
  void TryReadMoreReply();

  void SetReplyData(const char* data, size_t bytes);

  void SetReadWriteCallback(const BackendQuerySentCallback& query_sent_callback,
                            const BackendReplyReceivedCallback& reply_received_callback) {
    query_sent_callback_ = query_sent_callback;
    reply_received_callback_ = reply_received_callback;
  }
private:
  void HandleWrite(const char * buf, const size_t bytes, bool has_more_data,
      const boost::system::error_code& error, size_t bytes_transferred);
  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);
  void HandleConnect(const char * buf, size_t bytes, bool has_more_data, const boost::system::error_code& error);
public:
  void Close();
  void Reset();
  ReadBuffer* buffer() {
    return read_buffer_;
  }
  const ip::tcp::endpoint& remote_endpoint() const {
    return remote_endpoint_;
  }
  void set_reply_complete() {
    reply_complete_ = true;
  }
  void set_no_recycle() {
    no_recycle_ = true;
  }
  bool reply_complete() const {
    return reply_complete_;
  }
  bool recyclable() const {
    return !no_recycle_ && reply_complete_;
  }
private:
  WorkerContext& context_;
  ReadBuffer* read_buffer_;
  ip::tcp::endpoint remote_endpoint_;
  ip::tcp::socket socket_;

  BackendReplyReceivedCallback reply_received_callback_;
  BackendQuerySentCallback query_sent_callback_;

  bool is_reading_more_;
  bool reply_complete_;  // if reveived end of reply from backend server. TODO : rename to "received_end_of_reply_"
  bool no_recycle_;
};

}

#endif // _YARMPROXY_BACKEND_CONN_H_
