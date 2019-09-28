#ifndef _YARMPROXY_REDIS_DEL_COMMAND_H_
#define _YARMPROXY_REDIS_DEL_COMMAND_H_

#include <boost/asio.hpp>

#include "command.h"

namespace yarmproxy {
using namespace boost::asio;

namespace redis {
class BulkArray;
}

class RedisDelCommand : public Command {
public:
  RedisDelCommand(std::shared_ptr<ClientConnection> client, const redis::BulkArray& ba);
  virtual ~RedisDelCommand();

  virtual bool query_parsing_complete() override;
  void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

private:
  void StartWriteReply() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;
  void OnBackendConnectError(std::shared_ptr<BackendConn> backend) override;

  bool ProcessUnparsedPart() override;

  bool WriteQuery() override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend(bool) override;

  bool query_data_zero_copy() override {
    return true;
  }
  bool query_recv_complete() override;

private:
  struct DelSubquery {
    DelSubquery(const ip::tcp::endpoint& ep, const char* data, size_t present_bytes)
        : backend_endpoint_(ep)
        , keys_count_(1)
    {
      segments_.emplace_back(data, present_bytes);
    }

    ip::tcp::endpoint backend_endpoint_;
    std::shared_ptr<BackendConn> backend_;

    size_t keys_count_;
    size_t phase_ = 0;
    bool query_recv_complete_ = false;
    bool connect_error_ = false;
    std::list<std::pair<const char*, size_t>> segments_;
  };
  size_t unparsed_bulks_;
  bool init_write_query_ = true;
  std::map<ip::tcp::endpoint, std::shared_ptr<DelSubquery>> waiting_subqueries_;
  std::map<std::shared_ptr<BackendConn>, std::shared_ptr<DelSubquery>> pending_subqueries_;
  std::shared_ptr<DelSubquery> tail_query_;
  std::shared_ptr<BackendConn> replying_backend_;

  int total_del_count_ = 0;
private:
  void ActivateWaitingSubquery();
  void PushSubquery(const ip::tcp::endpoint& ep, const char* data, size_t bytes);
};

}

#endif // _YARMPROXY_REDIS_DEL_COMMAND_H_
