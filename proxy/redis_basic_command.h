#ifndef _YARMPROXY_REDIS_BASIC_COMMAND_H_
#define _YARMPROXY_REDIS_BASIC_COMMAND_H_

#include <boost/asio/ip/tcp.hpp>

#include "command.h"
#include "redis_protocol.h"

namespace yarmproxy {

using Endpoint = boost::asio::ip::tcp::endpoint;

class RedisBasicCommand: public Command {
public:
  RedisBasicCommand(std::shared_ptr<ClientConnection> client,
                    const redis::BulkArray& ba);

  virtual ~RedisBasicCommand();

private:
  void StartWriteReply() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

  bool WriteQuery() override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend(bool) override;

  bool query_data_zero_copy() override {
    return true;
  }
  bool query_recv_complete() override {
    return true;
  }

  size_t ParseQuery(const char* cmd_line, size_t cmd_len);
private:
  Endpoint backend_endpoint_;
  std::shared_ptr<BackendConn> backend_conn_;
};

}

#endif // _YARMPROXY_REDIS_BASIC_COMMAND_H_
