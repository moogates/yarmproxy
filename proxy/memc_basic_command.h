#ifndef _YARMPROXY_MEMC_BASIC_COMMAND_H_
#define _YARMPROXY_MEMC_BASIC_COMMAND_H_

#include <boost/asio/ip/tcp.hpp>

#include "command.h"

namespace yarmproxy {

using Endpoint = boost::asio::ip::tcp::endpoint;

class MemcBasicCommand: public Command {
public:
  MemcBasicCommand(std::shared_ptr<ClientConnection> client,
                   const char* buf, size_t cmd_len);
  virtual ~MemcBasicCommand();

private:
  bool StartWriteQuery() override;
  bool ContinueWriteQuery() override {
    assert(false);
    return false;
  }

  void StartWriteReply() override;
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
};

}

#endif // _YARMPROXY_MEMC_BASIC_COMMAND_H_
