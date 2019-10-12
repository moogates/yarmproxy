#ifndef _YARMPROXY_REDIS_MGET_COMMAND_H_
#define _YARMPROXY_REDIS_MGET_COMMAND_H_

#include <map>
#include <set>

#include "command.h"
#include "redis_protocol.h"

namespace yarmproxy {

class RedisMgetCommand : public Command {
public:
  RedisMgetCommand(std::shared_ptr<ClientConnection> client,
                  const redis::BulkArray& ba);

  virtual ~RedisMgetCommand();

  bool WriteQuery() override;
//void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend,
//                          ErrorCode ec) override;

  void StartWriteReply() override;
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
                              ErrorCode ec) override;
  void OnWriteReplyFinished(std::shared_ptr<BackendConn> backend,
                            ErrorCode ec) override;

private:
  bool BackendErrorRecoverable(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;
  void OnBackendRecoverableError(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;
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
  struct BackendQuery;
  std::string reply_prefix_;
  bool reply_prefix_complete() const {
    return reply_prefix_.empty();
  }
  void set_reply_prefix_complete() {
    reply_prefix_.clear();
  }

  std::vector<std::shared_ptr<BackendQuery>> subqueries_;
  std::list<std::shared_ptr<BackendConn>> waiting_reply_queue_;

  std::map<std::shared_ptr<BackendConn>, std::shared_ptr<BackendQuery>> backend_subqueries_;

  std::shared_ptr<BackendConn> last_backend_;

  size_t completed_backends_ = 0;
  std::set<std::shared_ptr<BackendConn>> received_reply_backends_;

  // std::map<std::shared_ptr<BackendConn>, size_t> absent_bulks_tracker_;
};

}

#endif  // _YARMPROXY_REDIS_MGET_COMMAND_H_

