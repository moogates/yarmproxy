#ifndef _CLIENT_CONNECTION_H_
#define _CLIENT_CONNECTION_H_

#include <list>
#include <queue>
#include <set>
#include <string>

#include <boost/asio.hpp>
#include <memory>

#include "base/logging.h"

using namespace boost::asio;

namespace mcproxy {

class BackendConnPool;
class MemcCommand;
struct WorkerContext;
class ReadBuffer;

typedef std::function<void(const boost::system::error_code& error)> ForwardResponseCallback;

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
  void OnCommandError(std::shared_ptr<MemcCommand> memc_cmd, const boost::system::error_code& error);

public:
  void ForwardResponse(const char* data, size_t bytes, const ForwardResponseCallback& cb);
  bool IsFirstCommand(std::shared_ptr<MemcCommand> cmd) {
    // TODO : 能否作为一个标记，放在command里面？
    return cmd == active_cmd_queue_.front();
  }
  void RotateFirstCommand();

  void TryReadMoreQuery();
  ReadBuffer* buffer() {
    return read_buffer_;
  }

  // boost::asio::io_service& io_service_;
private:
  ip::tcp::socket socket_;
public:
  ReadBuffer* read_buffer_;

protected:
  WorkerContext& context_;

private:
  std::list<std::shared_ptr<MemcCommand>> active_cmd_queue_;

  ForwardResponseCallback forward_resp_callback_;

  size_t timeout_;
  boost::asio::deadline_timer timer_;

  void AsyncRead();

  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);
  bool ProcessUnparsedData();

  void HandleMemcCommandTimeout(const boost::system::error_code& error);
  void HandleTimeoutWrite(const boost::system::error_code& error);
};

}

#endif // _CLIENT_CONNECTION_H_

