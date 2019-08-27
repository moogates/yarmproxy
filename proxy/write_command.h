#ifndef _WRITE_COMMAND_H_
#define _WRITE_COMMAND_H_

#include "command.h"

using namespace boost::asio;

namespace mcproxy {

class WriteCommand : public Command {
private:
//ip::tcp::endpoint backend_endpoint_;
//BackendConn* backend_conn_;

  const char * request_cmd_line_;
  size_t request_cmd_len_;

  size_t request_forwarded_bytes_;
  size_t request_body_bytes_;
  size_t bytes_forwarding_;

  ip::tcp::endpoint backend_endpoint_;
  BackendConn* backend_conn_;
public:
  WriteCommand(const ip::tcp::endpoint & ep, 
          std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len, size_t body_bytes);

  virtual ~WriteCommand();

  size_t request_body_upcoming_bytes() const override;
  void OnForwardQueryFinished(BackendConn* backend, const boost::system::error_code& error) override;

  size_t request_body_bytes() const override {  // for debug info only
    return request_body_bytes_;
  }
private:
  void OnForwardReplyEnabled() override {
    TryForwardReply(backend_conn_);
  }

  void ForwardQuery(const char * data, size_t bytes) override;
  bool ParseReply(BackendConn* backend) override;
  void DoForwardQuery(const char * request_data, size_t client_buf_received_bytes) override;

  std::string cmd_line_without_rn() const override {
    return std::string(request_cmd_line_, request_cmd_len_ - 2);
  }
};

}

#endif // _WRITE_COMMAND_H_
