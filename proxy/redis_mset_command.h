#ifndef _YARMPROXY_REDIS_MSET_COMMAND_H_
#define _YARMPROXY_REDIS_MSET_COMMAND_H_

#include <boost/asio.hpp>

#include "command.h"

namespace yarmproxy {
using namespace boost::asio;

namespace redis {
class BulkArray;
}

class RedisMsetCommand : public Command {
public:
  RedisMsetCommand(std::shared_ptr<ClientConnection> client, const redis::BulkArray& ba);
  virtual ~RedisMsetCommand();

  virtual bool query_parsing_complete() override;
  void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

private:
  void StartWriteReply() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;
  void OnBackendConnectError(std::shared_ptr<BackendConn> backend) override;

  bool ParseIncompleteQuery() override;

  void WriteQuery() override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend(bool) override;

  bool query_data_zero_copy() override {
    return true;
  }

private:
  struct Subquery {
    Subquery(const ip::tcp::endpoint& ep, size_t bulks_count, const char* data, size_t present_bytes)
        : backend_endpoint_(ep)
        , bulks_count_(bulks_count)
    {
      segments_.emplace_back(data, present_bytes);
    }

    ip::tcp::endpoint backend_endpoint_;
    std::shared_ptr<BackendConn> backend_;

    size_t bulks_count_;
    size_t phase_ = 0;
    bool query_recv_complete_ = false;
    bool connect_error_ = false;
    std::list<std::pair<const char*, size_t>> segments_;
  };

  bool suspect_ = false;
  size_t unparsed_bulks_;
  bool init_write_query_ = true;
  std::map<ip::tcp::endpoint, std::shared_ptr<Subquery>> subqueries_;
  std::map<std::shared_ptr<BackendConn>, std::shared_ptr<Subquery>> pending_subqueries_;
  std::shared_ptr<Subquery> tail_query_;
  std::shared_ptr<BackendConn> replying_backend_;
private:
  void ActivateWaitingSubquery();
  void PushSubquery(const ip::tcp::endpoint& ep, const char* data, size_t bytes);
};

}

#endif // _YARMPROXY_REDIS_MSET_COMMAND_H_
