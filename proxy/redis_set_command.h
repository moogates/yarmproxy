#ifndef _YARMPROXY_REDIS_SET_COMMAND_H_
#define _YARMPROXY_REDIS_SET_COMMAND_H_

#include <boost/asio.hpp>

#include "command.h"

namespace yarmproxy {
using namespace boost::asio;

namespace redis {
class BulkArray;
}

class RedisSetCommand : public Command {
public:
  RedisSetCommand(const ip::tcp::endpoint & ep, std::shared_ptr<ClientConnection> client, const redis::BulkArray& ba);
  RedisSetCommand(std::shared_ptr<ClientConnection> client, const redis::BulkArray& ba);
  virtual ~RedisSetCommand();

  virtual bool query_parsing_complete() override;

private:
  void StartWriteReply() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;
  void OnBackendConnectError(std::shared_ptr<BackendConn> backend) override;

  bool ParseIncompleteQuery() override;

  bool WriteQuery() override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend(bool) override;

  bool query_data_zero_copy() override {
    return true;
  }
  bool query_recv_complete() override {
    return query_recv_complete_;
  }

private:
  ip::tcp::endpoint backend_endpoint_;
  std::shared_ptr<BackendConn> backend_conn_;

  size_t unparsed_bulks_;
  bool query_recv_complete_ = false;
  bool connect_error_ = false;
};

}

#endif // _YARMPROXY_REDIS_SET_COMMAND_H_
