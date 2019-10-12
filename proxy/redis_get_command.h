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
  // void OnBackendRecoverableError(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend(bool success) override;

private:
  bool query_data_zero_copy() override {
    return true;
  }

private:
  const char* cmd_data_;
  size_t cmd_bytes_;
  Endpoint backend_endpoint_;
};

}

#endif  // _YARMPROXY_REDIS_GET_COMMAND_H_

