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

typedef std::function<void(ErrorCode ec)> WriteReplyCallback;

class Command : public std::enable_shared_from_this<Command> {
public:
  static int CreateCommand(std::shared_ptr<ClientConnection> client,
                           const char* buf, size_t size,
                           std::shared_ptr<Command>* cmd);
  Command(std::shared_ptr<ClientConnection> client, const std::string& original_header);

public:
  virtual ~Command();

  virtual void WriteQuery(const char * data, size_t bytes) = 0;

  // backend_conn转发完毕WriteQuery()指定的数据后，调用OnWriteQueryFinished()
  virtual void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) = 0;

  // backend_conn收到reply数据后, 调用OnBackendReplyReceived()
  void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec);
  virtual void OnWriteReplyEnabled() = 0;

  void OnWriteReplyFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec);
private:
  virtual void HookOnBackendReplyReceived(std::shared_ptr<BackendConn> backend){}
  virtual void RotateReplyingBackend();
protected:
  bool TryActivateReplyingBackend(std::shared_ptr<BackendConn> backend);

public:
  const std::string& original_header() const {
    return original_header_;
  }

private:
  virtual void DoWriteQuery(const char * data, size_t bytes) = 0;
  virtual bool ParseReply(std::shared_ptr<BackendConn> backend) = 0;
  // virtual size_t query_body_upcoming_bytes() const = 0;
protected:
  bool is_transfering_reply_;
  std::shared_ptr<BackendConn> replying_backend_;
  size_t completed_backends_;
  size_t unreachable_backends_;
  std::shared_ptr<ClientConnection> client_conn_;

  std::string original_header_;

  WorkerContext& context();

  void TryWriteReply(std::shared_ptr<BackendConn> backend);
  virtual void PushWaitingReplyQueue(std::shared_ptr<BackendConn> backend) {}
  virtual void OnBackendConnectError(std::shared_ptr<BackendConn> backend);

  typedef void(Command::*BackendCallbackFunc)(std::shared_ptr<BackendConn> backend, ErrorCode ec);
  WriteReplyCallback WeakBind(BackendCallbackFunc mem_func, std::shared_ptr<BackendConn> backend) {
    std::weak_ptr<Command> cmd_wptr(shared_from_this());
    std::weak_ptr<BackendConn> backend_wptr(backend);
    return [cmd_wptr, mem_func, backend_wptr](ErrorCode ec) {
          auto cmd_ptr = cmd_wptr.lock();
          auto backend_ptr = backend_wptr.lock();
          if (cmd_ptr && backend_ptr) {
            ((*cmd_ptr).*mem_func)(backend_ptr, ec);
          }
        };
  }

private:
  timeval time_created_;
  bool loaded_;
};

}

#endif // _YARMPROXY_COMMAND_H_

