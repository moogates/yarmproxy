#ifndef _YARMPROXY_MEMCACHED_SET_COMMAND_H_
#define _YARMPROXY_MEMCACHED_SET_COMMAND_H_

#include <boost/asio/ip/tcp.hpp>

#include "command.h"

namespace yarmproxy {
using Endpoint = boost::asio::ip::tcp::endpoint;

class MemcachedSetCommand : public Command {
public:
  MemcachedSetCommand(std::shared_ptr<ClientConnection> client,
            const char* buf,
            size_t cmd_len,
            size_t* body_bytes);

  virtual ~MemcachedSetCommand();

private:
  void StartWriteReply() override;
//void OnBackendRecoverableError(std::shared_ptr<BackendConn> backend,
//                               ErrorCode ec) override;

  bool WriteQuery() override;
  bool ContinueWriteQuery() override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend(bool) override;

  bool query_data_zero_copy() override {
    return true;
  }
  bool query_recv_complete() override;

  size_t ParseQuery(const char* cmd_line, size_t cmd_len);
private:
  Endpoint backend_endpoint_;
  std::shared_ptr<BackendConn> backend_conn_;

  bool query_recv_complete_ = false;
};

}

#endif // _YARMPROXY_MEMCACHED_SET_COMMAND_H_
