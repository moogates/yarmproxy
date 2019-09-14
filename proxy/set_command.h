#ifndef _YARMPROXY_SET_COMMAND_H_
#define _YARMPROXY_SET_COMMAND_H_

#include <boost/asio.hpp>

#include "command.h"

namespace yarmproxy {

using namespace boost::asio;

class SetCommand : public Command {
public:
  SetCommand(std::shared_ptr<ClientConnection> client,
            const char* buf,
            size_t cmd_len,
            size_t* body_bytes);

  virtual ~SetCommand();

private:
  void StartWriteReply() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

  void WriteQuery() override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend(bool) override;

  bool query_data_zero_copy() override {
    return true;
  }

  static int ParseCommandLine(const char* cmd_line, size_t cmd_len, std::string* key, size_t* bytes);
private:
  ip::tcp::endpoint backend_endpoint_;
  std::shared_ptr<BackendConn> backend_conn_;
};

}

#endif // _YARMPROXY_SET_COMMAND_H_
