#ifndef _YARMPROXY_SET_COMMAND_H_
#define _YARMPROXY_SET_COMMAND_H_

#include <boost/asio.hpp>

#include "command.h"

namespace yarmproxy {

using namespace boost::asio;

class SetCommand : public Command {
public:
  SetCommand(const ip::tcp::endpoint & ep,
          std::shared_ptr<ClientConnection> client,
          const char * buf, size_t cmd_len, size_t body_bytes);

  virtual ~SetCommand();

private:
  void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;
  void OnWriteReplyEnabled() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

  void WriteQuery() override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend(bool) override;

private:
  ip::tcp::endpoint backend_endpoint_;
  std::shared_ptr<BackendConn> backend_conn_;
};

}

#endif // _YARMPROXY_SET_COMMAND_H_
