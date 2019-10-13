#ifndef _YARMPROXY_REDIS_BASIC_COMMAND_H_
#define _YARMPROXY_REDIS_BASIC_COMMAND_H_

#include "command.h"
#include "redis_protocol.h"

namespace yarmproxy {

class RedisBasicCommand: public Command {
public:
  RedisBasicCommand(std::shared_ptr<ClientConnection> client,
                    const redis::BulkArray& ba);

  virtual ~RedisBasicCommand();

private:
  // size_t ParseQuery(const char* cmd_line, size_t cmd_len);
  bool ContinueWriteQuery() override {
    assert(false);
    return false;
  }

  // bool ParseReply(std::shared_ptr<BackendConn> backend) override;

  bool query_data_zero_copy() override {
    return true;
  }
  bool query_recv_complete() override {
    return true;
  }
};

}

#endif // _YARMPROXY_REDIS_BASIC_COMMAND_H_
