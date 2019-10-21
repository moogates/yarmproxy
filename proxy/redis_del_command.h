#ifndef _YARMPROXY_REDIS_DEL_COMMAND_H_
#define _YARMPROXY_REDIS_DEL_COMMAND_H_

#include <boost/asio/ip/tcp.hpp>

#include "command.h"

namespace yarmproxy {
using Endpoint = boost::asio::ip::tcp::endpoint;

namespace redis {
class BulkArray;
}

class RedisDelCommand : public Command {
public:
  RedisDelCommand(std::shared_ptr<ClientConnection> client, const redis::BulkArray& ba);
  virtual ~RedisDelCommand();

  bool query_parsing_complete() override;
  void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

private:
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;
  void OnBackendRecoverableError(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

  bool ProcessUnparsedPart() override;

  bool StartWriteQuery() override;
  bool ContinueWriteQuery() override {
    assert(false);
    return false;
  }

  bool ParseReply(std::shared_ptr<BackendConn> backend) override;

  bool query_data_zero_copy() override {
    return true;
  }
  bool query_recv_complete() override;

private:
  std::string cmd_name_;
  struct Subquery;
  size_t unparsed_bulks_;
  bool init_write_query_ = true; // TODO : remove it
  std::map<Endpoint, std::shared_ptr<Subquery>> waiting_subqueries_;
  std::map<std::shared_ptr<BackendConn>, std::shared_ptr<Subquery>> pending_subqueries_;

  int total_del_count_ = 0;
private:
  void ActivateWaitingSubquery();
  void PushSubquery(const Endpoint& ep, const char* data, size_t bytes);
};

}

#endif // _YARMPROXY_REDIS_DEL_COMMAND_H_
