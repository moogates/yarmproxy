#ifndef _YARMPROXY_STATS_COMMAND_H_
#define _YARMPROXY_STATS_COMMAND_H_

#include "command.h"

namespace yarmproxy {

enum class ProtocolType;

class StatsCommand : public Command {
public:
  StatsCommand(std::shared_ptr<ClientConnection> client,
               ProtocolType protocol);

  virtual ~StatsCommand();

private:
  bool StartWriteQuery() override;
  void StartWriteReply() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn>, ErrorCode) override {
    assert(false);
  }
  bool ParseReply(std::shared_ptr<BackendConn>) override {
    assert(false);
    return true;
  }
  void OnWriteReplyFinished(std::shared_ptr<BackendConn> backend,
                                   ErrorCode ec) override;
  bool query_recv_complete() override {
    return true;
  }
private:
  std::string reply_message_;
};

}

#endif // _YARMPROXY_STATS_COMMAND_H_
