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

typedef std::function<void(ErrorCode ec)> WriteReplyCallback;

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

public:
  void Abort();

public:
  void WriteReply(const char* data, size_t bytes, const WriteReplyCallback& cb);
  bool IsFirstCommand(std::shared_ptr<Command> cmd) {
    // TODO : 能否作为一个标记，放在command里面？
    return cmd == active_cmd_queue_.front();
  }
  void RotateReplyingCommand();

  void TryReadMoreQuery(const char* caller = ""); // TODO : call param for debug only
  ReadBuffer* buffer() {
    return buffer_;
  }

private:
  ip::tcp::socket socket_;
  ReadBuffer* buffer_;

protected:
  WorkerContext& context_;

private:
  std::list<std::shared_ptr<Command>> active_cmd_queue_;
  bool is_reading_query_ = false;
  // bool aborted_ = false; // TODO : requeired?

  void AsyncRead();

  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);
  bool ProcessUnparsedQuery();

  void IdleTimeout(const boost::system::error_code& error);
  void UpdateTimer();
  boost::asio::steady_timer timer_; // TODO : system_timer or steady_timer?
};

}

#endif // _YARMPROXY_CLIENT_CONNECTION_H_

