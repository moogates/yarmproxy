#ifndef _YARMPROXY_REDIS_MSET_COMMAND_H_
#define _YARMPROXY_REDIS_MSET_COMMAND_H_

#include <boost/asio/ip/tcp.hpp>

#include "command.h"

namespace yarmproxy {
using Endpoint = boost::asio::ip::tcp::endpoint;

namespace redis {
class Bulk;
class BulkArray;
}

class RedisMsetCommand : public Command {
public:
  RedisMsetCommand(std::shared_ptr<ClientConnection> client, const redis::BulkArray& ba);
  virtual ~RedisMsetCommand();

  void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

private:
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;
  void OnBackendRecoverableError(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

  bool ProcessUnparsedPart() override;

  bool StartWriteQuery() override;
  bool ContinueWriteQuery() override;

  bool query_parsing_complete() override;
  bool query_recv_complete() override;
private:
  struct Subquery;

  size_t unparsed_bulks_;
  std::map<Endpoint, std::shared_ptr<Subquery>> waiting_subqueries_;
  std::map<std::shared_ptr<BackendConn>, std::shared_ptr<Subquery>> pending_subqueries_;
  std::shared_ptr<Subquery> tail_query_;
private:
  void ActivateWaitingSubquery();
  void PushSubquery(const Endpoint& ep, const char* data, size_t bytes);
};

}

#endif // _YARMPROXY_REDIS_MSET_COMMAND_H_
