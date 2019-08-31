#ifndef _YARMPROXY_WRITE_COMMAND_H_
#define _YARMPROXY_WRITE_COMMAND_H_

#include <boost/asio.hpp>

#include "command.h"

namespace yarmproxy {

using namespace boost::asio;

class WriteCommand : public Command {
private:
  size_t query_header_bytes_;

  size_t query_forwarded_bytes_;
  size_t query_body_bytes_;
  size_t query_forwarding_bytes_;

  ip::tcp::endpoint backend_endpoint_;
  BackendConn* backend_conn_;
public:
  WriteCommand(const ip::tcp::endpoint & ep,
          std::shared_ptr<ClientConnection> client,
          const char * buf, size_t cmd_len, size_t body_bytes);

  virtual ~WriteCommand();

private:
  size_t query_body_upcoming_bytes() const override;
  void OnForwardQueryFinished(BackendConn* backend, ErrorCode ec) override;
  void OnForwardReplyEnabled() override {
    TryForwardReply(backend_conn_);
  }

  void ForwardQuery(const char * data, size_t bytes) override;
  bool ParseReply(BackendConn* backend) override;
  void DoForwardQuery(const char * query_data, size_t received_bytes) override;
};

}

#endif // _YARMPROXY_WRITE_COMMAND_H_
