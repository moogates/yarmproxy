#ifndef _YARMPROXY_REDIS_MGET_COMMAND_H_
#define _YARMPROXY_REDIS_MGET_COMMAND_H_

#include <set>

#include <boost/asio/ip/tcp.hpp>

#include "command.h"
#include "redis_protocol.h"

namespace yarmproxy {

using Endpoint = boost::asio::ip::tcp::endpoint;

class RedisMgetCommand : public Command {
public:
  RedisMgetCommand(std::shared_ptr<ClientConnection> client,
                  const redis::BulkArray& ba);

  virtual ~RedisMgetCommand();

  bool StartWriteQuery() override;
  bool ContinueWriteQuery() override {
    assert(false);
    return false;
  }

  void StartWriteReply() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
                              ErrorCode ec) override;
  void OnWriteReplyFinished(std::shared_ptr<BackendConn> backend,
                            ErrorCode ec) override;

private:
  bool BackendErrorRecoverable(std::shared_ptr<BackendConn> backend,
      ErrorCode ec) override;
  void OnBackendRecoverableError(std::shared_ptr<BackendConn> backend,
      ErrorCode ec) override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend() override;

private:
  void BackendReadyToReply(std::shared_ptr<BackendConn> backend);
  void NextBackendStartReply();

  bool query_data_zero_copy() override {
    return false; // a bit more copy, for less system call and simpler code
  }
private:
  struct Subquery;
  std::string reply_prefix_;
  bool reply_prefix_complete() const {
    return reply_prefix_.empty();
  }
  void set_reply_prefix_complete() {
    reply_prefix_.clear();
  }

  std::map<Endpoint, std::shared_ptr<Subquery>> subqueries_;
  std::list<std::pair<std::shared_ptr<BackendConn>, int>> waiting_reply_queue_;
};

}

#endif  // _YARMPROXY_REDIS_MGET_COMMAND_H_

