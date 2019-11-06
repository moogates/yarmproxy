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
  bool StartWriteQuery() override;
  void StartWriteReply() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
                              ErrorCode ec) override {
    assert(false);
  }
  bool ParseReply(std::shared_ptr<BackendConn> backend) override {
    assert(false);
    return true;
  }
  void OnWriteReplyFinished(std::shared_ptr<BackendConn> backend,
                                   ErrorCode ec) override;

  bool query_recv_complete() override {
    return true;
  }
private:
  std::string reply_message_ = "Bad Request";
};

}

#endif // _YARMPROXY_ERROR_COMMAND_H_
