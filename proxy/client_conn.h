#ifndef _CLIENT_CONNECTION_H_
#define _CLIENT_CONNECTION_H_

#include <list>
#include <queue>
#include <set>
#include <string>

#include <boost/asio.hpp>
#include <memory>

#include "base/logging.h"
#include "read_buffer.h"

using namespace boost::asio;

namespace mcproxy {

class BackendConnPool;
class MemcCommand;

typedef std::function<void(const boost::system::error_code& error)> ForwardResponseCallback;

class ClientConnection : public std::enable_shared_from_this<ClientConnection> {
public:
  ClientConnection(boost::asio::io_service& io_service, BackendConnPool* pool);
  ~ClientConnection();

  ip::tcp::socket& socket() {
    return socket_;
  }

  BackendConnPool* upconn_pool() {
    return upconn_pool_;
  }

  void Start();

  void OnCommandError(std::shared_ptr<MemcCommand> memc_cmd, const boost::system::error_code& error);

public:
  void ForwardResponse(const char* data, size_t bytes, const ForwardResponseCallback& cb);
  bool IsFirstCommand(std::shared_ptr<MemcCommand> cmd) {
    return cmd == poly_cmd_queue_.front();
  }
  void RotateFirstCommand();
  void TryReadMoreRequest();

protected:
  boost::asio::io_service& io_service_;
private:
  ip::tcp::socket socket_;
public:
  ReadBuffer read_buffer_;

protected:
  BackendConnPool* upconn_pool_;

private:
  ForwardResponseCallback forward_resp_callback_;

  std::list<std::shared_ptr<MemcCommand>> poly_cmd_queue_; // 新版支持多态的cmd

  size_t timeout_;
  boost::asio::deadline_timer timer_;

  void AsyncRead();

  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);
  bool ForwardParsedUnreceivedRequest(size_t last_parsed_unreceived_bytes);

  void HandleMemcCommandTimeout(const boost::system::error_code& error);
  void HandleTimeoutWrite(const boost::system::error_code& error);
};

}

#endif // _CLIENT_CONNECTION_H_

