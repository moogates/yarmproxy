#ifndef _PARALLEL_GET_COMMAND_H_
#define _PARALLEL_GET_COMMAND_H_

#include <unordered_map>

#include "memc_command.h"

using namespace boost::asio;

namespace mcproxy {

class ParallelGetCommand : public MemcCommand {
public:
  ParallelGetCommand(const ip::tcp::endpoint & ep, 
          std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len);
  virtual ~ParallelGetCommand();

  void OnForwardResponseReady() override;
  void OnUpstreamRequestWritten(const boost::system::error_code& error) override {
    // 不需要再通知Client Conn
  }
private:
  struct BackendTask {
    std::string sub_cmd_line_;
    BackendConn* backend_;
  };
  std::unordered_map<ip::tcp::endpoint, BackendTask> backend_map_;

  void DoForwardRequest(const char *, size_t) override;
  bool ParseUpstreamResponse() override;

  std::string cmd_line_without_rn() const override {
    return cmd_line_.substr(0, cmd_line_.size() - 2);
  }

  // std::string cmd_line_;
};

}

#endif  // _PARALLEL_GET_COMMAND_H_

