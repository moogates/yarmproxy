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
  void TryMarkLastBackend(std::shared_ptr<BackendConn> backend);
  void BackendReadyToReply(std::shared_ptr<BackendConn> backend);

  bool HasUnfinishedBanckends() const;
  void NextBackendStartReply();
  bool TryActivateReplyingBackend(std::shared_ptr<BackendConn> backend);

  bool query_data_zero_copy() override {
    return false;
  }

private:
  struct BackendQuery {
    BackendQuery(const ip::tcp::endpoint& ep, std::string&& query_line)
        : query_line_(query_line)
        , backend_endpoint_(ep) {
    }
    ~BackendQuery();
    std::string query_line_;
    ip::tcp::endpoint backend_endpoint_;
    std::shared_ptr<BackendConn> backend_conn_;
  };

  std::vector<std::unique_ptr<BackendQuery>> query_set_;
  std::list<std::shared_ptr<BackendConn>> waiting_reply_queue_;

  std::shared_ptr<BackendConn> replying_backend_;
  std::shared_ptr<BackendConn> last_backend_;

  size_t completed_backends_;
  size_t unreachable_backends_;
  std::set<std::shared_ptr<BackendConn>> received_reply_backends_;
/////////////////////////
  std::shared_ptr<BackendConn> first_reply_backend_;
};

}

#endif  // _YARMPROXY_REDIS_GET_COMMAND_H_

