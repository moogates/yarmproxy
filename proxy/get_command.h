#ifndef _GET_COMMAND_H_
#define _GET_COMMAND_H_

#include "memc_command.h"

using namespace boost::asio;

namespace mcproxy {

class SingleGetCommand : public MemcCommand {
public:
  SingleGetCommand(const ip::tcp::endpoint & ep, 
          std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len);
  virtual ~SingleGetCommand();

  void OnForwardResponseReady() override;
  void OnUpstreamRequestWritten(const boost::system::error_code& error) override {
    // 不需要再通知Client Conn
  }
private:
  void DoForwardRequest(const char *, size_t) override;
  bool ParseUpstreamResponse() override;
  std::string cmd_line_without_rn() const override {
    return cmd_line_.substr(0, cmd_line_.size() - 2);
  }

  std::string cmd_line_;
};

}

#endif  // _GET_COMMAND_H_

