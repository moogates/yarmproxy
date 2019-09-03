#ifndef _YARMPROXY_MONO_GET_COMMAND_H_
#define _YARMPROXY_MONO_GET_COMMAND_H_

#include <boost/asio.hpp>

#include "command.h"

namespace yarmproxy {

using namespace boost::asio;

class MonoGetCommand : public Command {
public:
  MonoGetCommand(const ip::tcp::endpoint & ep,
          std::shared_ptr<ClientConnection> client,
          const char * buf,
          size_t cmd_len);
  virtual ~MonoGetCommand();

private:
  void WriteQuery(const char * data, size_t bytes) override;
  void OnWriteReplyEnabled() override {
    TryWriteReply(backend_conn_);
  }

  void DoWriteQuery(const char *, size_t) override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

//size_t query_body_upcoming_bytes() const override {
//  return 0;
//}

  std::string cmd_line_;
  ip::tcp::endpoint backend_endpoint_;
  std::shared_ptr<BackendConn> backend_conn_;
};

}

#endif  // _YARMPROXY_MONO_GET_COMMAND_H_

