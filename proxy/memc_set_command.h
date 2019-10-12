#ifndef _YARMPROXY_MEMC_SET_COMMAND_H_
#define _YARMPROXY_MEMC_SET_COMMAND_H_

#include <boost/asio/ip/tcp.hpp>

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
  void StartWriteReply() override;
  // bool WriteQuery() override;
  void update_check_query_recv_complete() override;

  bool ContinueWriteQuery() override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend(bool) override;

  bool query_data_zero_copy() override {
    return true;
  }
  bool query_recv_complete() override;

  size_t ParseQuery(const char* cmd_line, size_t cmd_len);
private:
  bool query_recv_complete_ = false;
};

}

#endif // _YARMPROXY_MEMC_SET_COMMAND_H_
