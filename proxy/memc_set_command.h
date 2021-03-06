#ifndef _YARMPROXY_MEMC_SET_COMMAND_H_
#define _YARMPROXY_MEMC_SET_COMMAND_H_

#include "command.h"

namespace yarmproxy {

class MemcSetCommand : public Command {
public:
  MemcSetCommand(std::shared_ptr<ClientConnection> client,
            const char* buf,
            size_t cmd_len,
            size_t* body_bytes);

  virtual ~MemcSetCommand();

private:
  size_t ParseQuery(const char* cmd_line, size_t cmd_len);
  void check_query_recv_complete() override;
  bool query_recv_complete() override {
    return query_recv_complete_;
  }
private:
  bool query_recv_complete_ = false;
};

}

#endif // _YARMPROXY_MEMC_SET_COMMAND_H_
