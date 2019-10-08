#ifndef _YARMPROXY_CLIENT_CONNECTION_H_
#define _YARMPROXY_CLIENT_CONNECTION_H_

#include <list>
#include <queue>
#include <set>
#include <string>
#include <memory>

#include <boost/asio.hpp>

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

  boost::asio::ip::tcp::socket& socket() {
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
  bool aborted() const { // TODO : for debug only
    return aborted_;
  }
  bool is_writing_reply() const {
    return is_writing_reply_;
  }

private:
  boost::asio::ip::tcp::socket socket_;
  ReadBuffer* buffer_;

protected:
  WorkerContext& context_;

private:
  std::list<std::shared_ptr<Command>> active_cmd_queue_;
  bool is_reading_query_ = false;
  bool is_writing_reply_ = false;
  bool aborted_ = false;

  void AsyncRead();

  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);
  bool ProcessUnparsedQuery();

  boost::asio::steady_timer read_timer_; // TODO : system_timer or steady_timer?
  boost::asio::steady_timer write_timer_; // TODO : system_timer or steady_timer?
  void OnReadTimeout(const boost::system::error_code& error);
  void OnWriteTimeout(const boost::system::error_code& error);
  void UpdateReadTimer();
  void UpdateWriteTimer();
};

}

#endif // _YARMPROXY_CLIENT_CONNECTION_H_

