#ifndef _YARMPROXY_MC_SIMPLE_COMMAND_H_
#define _YARMPROXY_MC_SIMPLE_COMMAND_H_

#include <boost/asio.hpp>

#include "command.h"

namespace yarmproxy {

using namespace boost::asio;

class MemcachedSimpleCommand: public Command {
public:
  MemcachedSimpleCommand(std::shared_ptr<ClientConnection> client,
            const char* buf,
            size_t cmd_len);

  virtual ~MemcachedSimpleCommand();

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
  ip::tcp::endpoint backend_endpoint_;
  std::shared_ptr<BackendConn> backend_conn_;
};

}

#endif // _YARMPROXY_MC_SIMPLE_COMMAND_H_
