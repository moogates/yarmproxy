#ifndef _GET_COMMAND_H_
#define _GET_COMMAND_H_

#include "command.h"

using namespace boost::asio;

namespace mcproxy {

class SingleGetCommand : public Command {
public:
  SingleGetCommand(const ip::tcp::endpoint & ep, 
          std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len);
  virtual ~SingleGetCommand();

private:
  void ForwardQuery(const char * data, size_t bytes) override;
  void OnForwardReplyEnabled() override {
    TryForwardReply(backend_conn_);
  }

  void DoForwardQuery(const char *, size_t) override;
  bool ParseReply(BackendConn* backend) override;
  void OnForwardQueryFinished(BackendConn* backend, const boost::system::error_code& error) override;

  std::string cmd_line_without_rn() const override {
    return cmd_line_.substr(0, cmd_line_.size() - 2);
  }
  size_t request_body_upcoming_bytes() const override {
    return 0;
  }

  std::string cmd_line_;
  ip::tcp::endpoint backend_endpoint_;
  BackendConn* backend_conn_;
};

}

#endif  // _GET_COMMAND_H_
