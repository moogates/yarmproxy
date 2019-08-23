#ifndef _PARALLEL_GET_COMMAND_H_
#define _PARALLEL_GET_COMMAND_H_

#include "memc_command.h"

using namespace boost::asio;

namespace mcproxy {

class ParallelGetCommand : public MemcCommand {
public:
  ParallelGetCommand(const ip::tcp::endpoint & ep, 
          std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len);
  virtual ~ParallelGetCommand();

private:
  void DoForwardRequest(const char *, size_t) override;
  bool ParseUpstreamResponse(BackendConn* backend) override;

  std::string cmd_line_without_rn() const override {
    return cmd_line_.substr(0, cmd_line_.size() - 2);
  }
  size_t request_body_upcoming_bytes() const override {
    return 0;
  }

  std::string cmd_line_;
};

}

#endif  // _PARALLEL_GET_COMMAND_H_

