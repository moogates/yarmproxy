#ifndef _YARMPROXY_COMMAND_H_
#define _YARMPROXY_COMMAND_H_

#include <list>
#include <vector>
#include <string>
#include <memory>

namespace yarmproxy {

class BackendConn;
class WorkerContext;
class ClientConnection;

enum class ErrorCode;

typedef std::function<void(ErrorCode ec)> ForwardReplyCallback;

class Command : public std::enable_shared_from_this<Command> {
public:
  static int CreateCommand(std::shared_ptr<ClientConnection> client,
                           const char* buf, size_t size,
                           std::shared_ptr<Command>* cmd);
  Command(std::shared_ptr<ClientConnection> client, const std::string& original_header);

public:
  virtual ~Command();

  virtual void ForwardQuery(const char * data, size_t bytes) = 0;

  // backend_conn转发完毕ForwardQuery()指定的数据后，调用OnForwardQueryFinished()
  virtual void OnForwardQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) = 0;

  // backend_conn收到reply数据后, 调用OnUpstreamReplyReceived()
  void OnUpstreamReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec);
  virtual void OnForwardReplyEnabled() = 0;

  void OnForwardReplyFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec);
private:
  virtual void HookOnUpstreamReplyReceived(std::shared_ptr<BackendConn> backend){}
  virtual void RotateReplyingBackend();
  virtual bool HasMoreBackend() const { // rename -> HasUnfinishedBanckends()
    return false;
  }
protected:
  bool TryActivateReplyingBackend(std::shared_ptr<BackendConn> backend);

public:
  const std::string& original_header() const {
    return original_header_;
  }

private:
  virtual void DoForwardQuery(const char * data, size_t bytes) = 0;
  virtual bool ParseReply(std::shared_ptr<BackendConn> backend) = 0;
  virtual size_t query_body_upcoming_bytes() const = 0;
protected:
  bool is_transfering_reply_;
  std::shared_ptr<BackendConn> replying_backend_;
  size_t completed_backends_;
  size_t unreachable_backends_;
  std::shared_ptr<ClientConnection> client_conn_;

  std::string original_header_;

  WorkerContext& context();

  void TryForwardReply(std::shared_ptr<BackendConn> backend);
  virtual void PushWaitingReplyQueue(std::shared_ptr<BackendConn> backend) {}
  virtual void OnBackendConnectError(std::shared_ptr<BackendConn> backend);

  typedef void(Command::*BackendCallbackFunc)(std::shared_ptr<BackendConn> backend, ErrorCode ec);
  ForwardReplyCallback WeakBind(BackendCallbackFunc mem_func, std::shared_ptr<BackendConn> backend) {
    std::weak_ptr<Command> cmd_wptr(shared_from_this());
    return [cmd_wptr, mem_func, backend](ErrorCode ec) {
          if (auto cmd_ptr = cmd_wptr.lock()) {
            ((*cmd_ptr).*mem_func)(backend, ec);
          }
        };
  }

private:
  timeval time_created_;
  bool loaded_;
};

}

#endif // _YARMPROXY_COMMAND_H_

