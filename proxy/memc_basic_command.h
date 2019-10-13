#ifndef _YARMPROXY_MEMC_BASIC_COMMAND_H_
#define _YARMPROXY_MEMC_BASIC_COMMAND_H_

#include "command.h"

namespace yarmproxy {

class MemcBasicCommand: public Command {
public:
  MemcBasicCommand(std::shared_ptr<ClientConnection> client,
                   const char* buf, size_t cmd_len);
  virtual ~MemcBasicCommand();

private:
  bool ContinueWriteQuery() override {
    assert(false);
    return false;
  }
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  bool query_data_zero_copy() override {
    return true;
  }
  bool query_recv_complete() override {
    return true;
  }
  size_t ParseQuery(const char* cmd_line, size_t cmd_len);
};

}

#endif // _YARMPROXY_MEMC_BASIC_COMMAND_H_
