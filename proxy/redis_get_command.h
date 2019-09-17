#ifndef _YARMPROXY_REDIS_GET_COMMAND_H_
#define _YARMPROXY_REDIS_GET_COMMAND_H_

#include <map>
#include <set>

#include <boost/asio.hpp>

#include "command.h"
#include "redis_protocol.h"

namespace yarmproxy {

using namespace boost::asio;

class RedisGetCommand : public Command {
public:
  RedisGetCommand(std::shared_ptr<ClientConnection> client,
                  const redis::BulkArray& ba);

  virtual ~RedisGetCommand();

  void WriteQuery() override;

  void StartWriteReply() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

private:
  // void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;
  void OnBackendConnectError(std::shared_ptr<BackendConn> backend) override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend(bool success) override;

private:
  bool HasUnfinishedBanckends() const;
  void NextBackendStartReply();

  bool query_data_zero_copy() override {
    return true;
  }

private:
  struct BackendQuery {
    BackendQuery(const ip::tcp::endpoint& ep, const char* cmd_data, size_t cmd_bytes)
        : cmd_data_(cmd_data)
        , cmd_bytes_(cmd_bytes)
        , backend_endpoint_(ep) {
    }
    ~BackendQuery();
    const char* cmd_data_;
    size_t cmd_bytes_;
    ip::tcp::endpoint backend_endpoint_;
    std::shared_ptr<BackendConn> backend_conn_;
  };

  std::unique_ptr<BackendQuery> backend_query_;
};

}

#endif  // _YARMPROXY_REDIS_GET_COMMAND_H_

