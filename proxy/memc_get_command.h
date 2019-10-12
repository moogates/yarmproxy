#ifndef _YARMPROXY_MEMC_GET_COMMAND_H_
#define _YARMPROXY_MEMC_GET_COMMAND_H_

#include <map>
#include <set>

#include "command.h"

namespace yarmproxy {

class MemcGetCommand : public Command {
public:
  MemcGetCommand(std::shared_ptr<ClientConnection> client,
                     const char* cmd_data, size_t cmd_size);

  virtual ~MemcGetCommand();

  bool StartWriteQuery() override;
  void StartWriteReply() override;
  bool ContinueWriteQuery() override {
    assert(false);
    return false;
  }
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

private:
  // void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;
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
    return false;
  }

private:
  static size_t ParseReplyBodyBytes(const char * data, const char * end);
  void ParseQuery(const char* cmd_data, size_t cmd_size);

  struct BackendQuery;
  std::vector<std::unique_ptr<BackendQuery>> subqueries_;
  std::list<std::shared_ptr<BackendConn>> waiting_reply_queue_;

  std::shared_ptr<BackendConn> last_backend_;

  size_t completed_backends_ = 0;
  std::set<std::shared_ptr<BackendConn>> received_reply_backends_;
};

}

#endif  // _YARMPROXY_MEMC_GET_COMMAND_H_

