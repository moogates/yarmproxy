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

  bool ParseIncompleteQuery() override;

  void WriteQuery() override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend(bool) override;

  bool query_data_zero_copy() override {
    return true;
  }

private:
  size_t unparsed_bulks_;

  struct Subquery {
    Subquery(const ip::tcp::endpoint& ep, size_t bulks_count, const char* data, size_t present_bytes, size_t index)
        : backend_endpoint_(ep)
        , bulks_count_(bulks_count)
        , data_(data)
        , present_bytes_(present_bytes)
        , phase_(0)
        , index_(index)
    {
    }

    ip::tcp::endpoint backend_endpoint_;

    size_t bulks_count_;
    const char* data_;
    size_t present_bytes_;
    size_t phase_;
    size_t index_;

    std::shared_ptr<BackendConn> backend_;
  };

  size_t subquery_index_;
  std::list<std::shared_ptr<Subquery>> waiting_subqueries_;
  void PushSubquery(const ip::tcp::endpoint& ep, const char* data, size_t bytes);

  // std::map<std::shared_ptr<BackendConn>, size_t> backend_index_;
  std::shared_ptr<Subquery> tail_query_;

  std::map<std::shared_ptr<BackendConn>, std::shared_ptr<Subquery>> pending_subqueries_;

  size_t completed_backends_;
  size_t unreachable_backends_;

  bool init_write_query_;

  // std::set<std::shared_ptr<BackendConn>> received_reply_backends_;
private:
  void ActivateWaitingSubquery();
};

}

#endif // _YARMPROXY_REDIS_MSET_COMMAND_H_
