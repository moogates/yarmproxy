#ifndef _YARMPROXY_MEMC_GET2_COMMAND_H_
#define _YARMPROXY_MEMC_GET2_COMMAND_H_

#include <map>
#include <set>

#include "command.h"

namespace yarmproxy {

class MemcGet2Command : public Command {
public:
  MemcGet2Command(std::shared_ptr<ClientConnection> client,
                     const char* cmd_data, size_t cmd_size);

  virtual ~MemcGet2Command();

  bool StartWriteQuery() override;
  void StartWriteReply() override;
  bool ContinueWriteQuery() override {
    assert(false);
    return false;
  }
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
      ErrorCode ec) override;

private:
  bool BackendErrorRecoverable(std::shared_ptr<BackendConn> backend,
      ErrorCode ec) override;
  void OnBackendRecoverableError(std::shared_ptr<BackendConn> backend,
      ErrorCode ec) override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void RotateReplyingBackend() override;

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
  static size_t ParseReplyBodySize(const char * data, const char * end);
  void ParseQuery(const char* cmd_data, size_t cmd_size);

  struct Subquery;
  std::vector<std::unique_ptr<Subquery>> subqueries_;
  std::list<std::shared_ptr<BackendConn>> waiting_reply_queue_;

  std::shared_ptr<BackendConn> last_backend_;

  size_t completed_backends_ = 0;
  std::set<std::shared_ptr<BackendConn>> received_reply_backends_;
};

}

#endif  // _YARMPROXY_MEMC_GET2_COMMAND_H_

