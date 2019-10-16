#ifndef _YARMPROXY_REDIS_SET_COMMAND_H_
#define _YARMPROXY_REDIS_SET_COMMAND_H_

#include "command.h"

namespace yarmproxy {

namespace redis {
class BulkArray;
}

class RedisSetCommand : public Command {
public:
  RedisSetCommand(std::shared_ptr<ClientConnection> client,
                  const redis::BulkArray& ba);
  virtual ~RedisSetCommand();

private:
  bool ParseUnparsedPart() override;
  bool query_data_zero_copy() override {
    return true;
  }
  bool query_parsing_complete() override;
  void check_query_recv_complete() override;
  bool query_recv_complete() override {
    return query_recv_complete_;
  }
private:
  size_t unparsed_bulks_;
  bool query_recv_complete_ = false;
};

}

#endif // _YARMPROXY_REDIS_SET_COMMAND_H_
