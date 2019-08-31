#ifndef _YARMPROXY_SINGLE_GET_COMMAND_H_
#define _YARMPROXY_SINGLE_GET_COMMAND_H_

#include <boost/asio.hpp>

#include "command.h"

namespace yarmproxy {

using namespace boost::asio;

class SingleGetCommand : public Command {
public:
  SingleGetCommand(const ip::tcp::endpoint & ep,
          std::shared_ptr<ClientConnection> client,
          const char * buf,
          size_t cmd_len);
  virtual ~SingleGetCommand();

private:
  void ForwardQuery(const char * data, size_t bytes) override;
  void OnForwardReplyEnabled() override {
    TryForwardReply(backend_conn_);
  }

  void DoForwardQuery(const char *, size_t) override;
  bool ParseReply(BackendConn* backend) override;
  void OnForwardQueryFinished(BackendConn* backend, ErrorCode ec) override;

  size_t query_body_upcoming_bytes() const override {
    return 0;
  }

  std::string cmd_line_;
  ip::tcp::endpoint backend_endpoint_;
  BackendConn* backend_conn_;
};

}

#endif  // _YARMPROXY_SINGLE_GET_COMMAND_H_

