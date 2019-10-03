#ifndef _YARMPROXY_STATS_COMMAND_H_
#define _YARMPROXY_STATS_COMMAND_H_

#include <boost/asio.hpp>

#include "command.h"

namespace yarmproxy {

using namespace boost::asio;
enum class ProtocolType;

class StatsCommand : public Command {
public:
  StatsCommand(std::shared_ptr<ClientConnection> client,
               ProtocolType protocol);

  virtual ~StatsCommand();

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
  ProtocolType protocol_;
  std::string reply_message_;
};

}

#endif // _YARMPROXY_STATS_COMMAND_H_
