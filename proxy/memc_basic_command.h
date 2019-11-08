#ifndef _YARMPROXY_MEMC_BASIC_COMMAND_H_
#define _YARMPROXY_MEMC_BASIC_COMMAND_H_

#include "command.h"

namespace yarmproxy {

class MemcBasicCommand: public Command {
public:
  MemcBasicCommand(std::shared_ptr<ClientConnection> client,
                   const char* buf);
  virtual ~MemcBasicCommand();

private:
  bool ContinueWriteQuery() override {
    assert(false);
    return false;
  }
  bool query_recv_complete() override {
    return true;
  }
};

}

#endif // _YARMPROXY_MEMC_BASIC_COMMAND_H_
