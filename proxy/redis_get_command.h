#ifndef _YARMPROXY_REDIS_GET_COMMAND_H_
#define _YARMPROXY_REDIS_GET_COMMAND_H_

#include <map>
#include <set>

#include <boost/asio/ip/tcp.hpp>

#include "command.h"
#include "redis_protocol.h"

namespace yarmproxy {

using Endpoint = boost::asio::ip::tcp::endpoint;

class RedisGetCommand : public Command {
public:
  RedisGetCommand(std::shared_ptr<ClientConnection> client,
                  const redis::BulkArray& ba);

  virtual ~RedisGetCommand();

  bool WriteQuery() override;

  void StartWriteReply() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

private:
  // void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;
  void OnBackendConnectError(std::shared_ptr<BackendConn> backend) override;
  void OnBackendError(std::shared_ptr<BackendConn> backend, ErrorCode ec); // TODO : base class need it!

  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend(bool success) override;

private:
  bool query_data_zero_copy() override {
    return true;
  }

private:
  bool has_read_some_reply_ = false; // TODO : Error Handling : fast fail vs. best effort?
  const char* cmd_data_;
  size_t cmd_bytes_;
  Endpoint backend_endpoint_;
  std::shared_ptr<BackendConn> backend_conn_;
};

}

#endif  // _YARMPROXY_REDIS_GET_COMMAND_H_

