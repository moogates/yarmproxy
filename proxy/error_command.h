#ifndef _YARMPROXY_ERROR_COMMAND_H_
#define _YARMPROXY_ERROR_COMMAND_H_

#include "command.h"

namespace yarmproxy {

class ErrorCommand : public Command {
public:
  ErrorCommand(std::shared_ptr<ClientConnection> client,
               const std::string& reply_message);

  virtual ~ErrorCommand();

private:
  bool WriteQuery() override;
  void StartWriteReply() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) override {
  }
  bool ParseReply(std::shared_ptr<BackendConn> backend) override {
    return true;
  }
  void OnWriteReplyFinished(std::shared_ptr<BackendConn> backend,
                                   ErrorCode ec) override;
  void RotateReplyingBackend(bool) override;

  bool query_data_zero_copy() override {
    return true;
  }
  bool query_recv_complete() override {
    return true;
  }
private:
  std::string reply_message_ = "Bad Request";
};

}

#endif // _YARMPROXY_ERROR_COMMAND_H_
