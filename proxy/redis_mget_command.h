#ifndef _YARMPROXY_REDIS_MGET_COMMAND_H_
#define _YARMPROXY_REDIS_MGET_COMMAND_H_

#include <map>
#include <set>

#include <boost/asio.hpp>

#include "command.h"
#include "redis_protocol.h"

namespace yarmproxy {

using namespace boost::asio;

class RedisMgetCommand : public Command {
public:
  RedisMgetCommand(std::shared_ptr<ClientConnection> client,
                  const redis::BulkArray& ba);

  virtual ~RedisMgetCommand();

  bool WriteQuery() override;
  void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend,
                            ErrorCode ec) override;

  void StartWriteReply() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
                              ErrorCode ec) override;
  void OnWriteReplyFinished(std::shared_ptr<BackendConn> backend,
                            ErrorCode ec) override;

private:
  void OnBackendConnectError(std::shared_ptr<BackendConn> backend) override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend(bool success) override;

private:
  void TryMarkLastBackend(std::shared_ptr<BackendConn> backend);
  void BackendReadyToReply(std::shared_ptr<BackendConn> backend);

  bool HasUnfinishedBanckends() const;
  void NextBackendStartReply();
  bool TryActivateReplyingBackend(std::shared_ptr<BackendConn> backend);

  bool query_data_zero_copy() override {
    return false; // a bit more copy, for less system call
  }
  bool ParseQuery(const redis::BulkArray& ba);
private:
  struct BackendQuery {
    BackendQuery(const ip::tcp::endpoint& ep, std::string&& query_data, size_t key_count)
        : backend_endpoint_(ep)
        , query_data_(query_data)
        , key_count_(key_count) {
    }
    ip::tcp::endpoint backend_endpoint_;
    std::string query_data_;

    size_t key_count_;
    size_t reply_absent_bulks_ = 0;
    std::shared_ptr<BackendConn> backend_conn_;
  };
  std::string reply_prefix_;
  bool reply_prefix_complete_ = false;

  std::vector<std::shared_ptr<BackendQuery>> subqueries_;
  std::list<std::shared_ptr<BackendConn>> waiting_reply_queue_;

  std::map<std::shared_ptr<BackendConn>, std::shared_ptr<BackendQuery>> backend_subqueries_;

  std::shared_ptr<BackendConn> replying_backend_;
  std::shared_ptr<BackendConn> last_backend_;

  size_t completed_backends_ = 0;
  size_t unreachable_backends_ = 0;
  std::set<std::shared_ptr<BackendConn>> received_reply_backends_;

  // std::map<std::shared_ptr<BackendConn>, size_t> absent_bulks_tracker_;
};

}

#endif  // _YARMPROXY_REDIS_MGET_COMMAND_H_

