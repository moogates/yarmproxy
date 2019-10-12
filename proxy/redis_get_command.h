#ifndef _YARMPROXY_REDIS_GET_COMMAND_H_
#define _YARMPROXY_REDIS_GET_COMMAND_H_

#include <map>
#include <set>

#include "command.h"
#include "redis_protocol.h"

namespace yarmproxy {

class RedisGetCommand : public Command {
public:
  RedisGetCommand(std::shared_ptr<ClientConnection> client,
                  const redis::BulkArray& ba);

  virtual ~RedisGetCommand();

#ifdef DEBUG
  bool ContinueWriteQuery() override {
    assert(false);
    return false;
  }
#endif

  void StartWriteReply() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

private:
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend(bool success) override;

private:
  bool query_data_zero_copy() override {
    return true;
  }

private:
  const char* cmd_data_;
  size_t cmd_bytes_;
};

}

#endif  // _YARMPROXY_REDIS_GET_COMMAND_H_

