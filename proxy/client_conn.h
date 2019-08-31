#ifndef _YARMPROXY_CLIENT_CONNECTION_H_
#define _YARMPROXY_CLIENT_CONNECTION_H_

#include <list>
#include <queue>
#include <set>
#include <string>
#include <memory>

#include <boost/asio.hpp>

using namespace boost::asio;

namespace yarmproxy {

class BackendConnPool;
class Command;
class WorkerContext;
class ReadBuffer;

enum class ErrorCode;

// typedef std::function<void(const boost::system::error_code& error)> ForwardReplyCallback;
typedef std::function<void(ErrorCode ec)> ForwardReplyCallback;

class ClientConnection : public std::enable_shared_from_this<ClientConnection> {
public:
  ClientConnection(WorkerContext& context);
  ~ClientConnection();

  WorkerContext& context() {
    return context_;
  }

  ip::tcp::socket& socket() {
    return socket_;
  }
  void StartRead();
  void Close();

public:
  void ErrorSilence();
  void ErrorReport(const char* msg, size_t bytes);
  void ErrorAbort();

public:
  void ForwardReply(const char* data, size_t bytes, const ForwardReplyCallback& cb);
  bool IsFirstCommand(std::shared_ptr<Command> cmd) {
    // TODO : 能否作为一个标记，放在command里面？
    return cmd == active_cmd_queue_.front();
  }
  void RotateReplyingCommand();

  void TryReadMoreQuery();
  ReadBuffer* buffer() {
    return read_buffer_;
  }

private:
  ip::tcp::socket socket_;
  ReadBuffer* read_buffer_;

protected:
  WorkerContext& context_;

private:
  std::list<std::shared_ptr<Command>> active_cmd_queue_;

  // ForwardReplyCallback forward_resp_callback_;

  size_t timeout_;
  boost::asio::deadline_timer timer_;

  void AsyncRead();

  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);
  bool ProcessUnparsedQuery();

  void HandleMemcCommandTimeout(const boost::system::error_code& error);
  void HandleTimeoutWrite(const boost::system::error_code& error);
};

}

#endif // _YARMPROXY_CLIENT_CONNECTION_H_

