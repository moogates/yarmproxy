#ifndef _YARMPROXY_COMMAND_H_
#define _YARMPROXY_COMMAND_H_

#include <cassert>

#include <functional>
#include <list>
#include <memory>
#include <vector>
#include <string>

#include "protocol_type.h"
namespace yarmproxy {

class BackendConn;
class BackendConnPool;
class KeyLocator;
class ClientConnection;

enum class ErrorCode;

typedef std::function<void(ErrorCode ec)> WriteReplyCallback;

class Command : public std::enable_shared_from_this<Command> {
public:
  static size_t CreateCommand(std::shared_ptr<ClientConnection> client,
                           const char* buf, size_t size,
                           std::shared_ptr<Command>* cmd);
  virtual ~Command();
  virtual bool StartWriteQuery();
  virtual bool ContinueWriteQuery();
  virtual void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
                                      ErrorCode ec);
  virtual void StartWriteReply();

  virtual void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend,
                                    ErrorCode ec);
  virtual void OnWriteReplyFinished(std::shared_ptr<BackendConn> backend,
                                    ErrorCode ec);

  virtual bool query_parsing_complete() {
    return true;
  }
  virtual void check_query_recv_complete() {
  }
  virtual bool query_recv_complete() {
    return true;
  }
  virtual bool BackendErrorRecoverable(std::shared_ptr<BackendConn> backend, ErrorCode ec);

  virtual bool ParseUnparsedPart() { return true; }
  virtual bool ProcessUnparsedPart() { return true; }
protected:
  Command(std::shared_ptr<ClientConnection> client, ProtocolType protocol);

  BackendConnPool* backend_pool();
  std::shared_ptr<KeyLocator> key_locator();

  void TryWriteReply(std::shared_ptr<BackendConn> backend);
  virtual void OnBackendRecoverableError(std::shared_ptr<BackendConn> backend, ErrorCode ec);

  const std::string& ErrorReply(ErrorCode ec);
  static const std::string& RedisErrorReply(ErrorCode ec);
  static const std::string& MemcErrorReply(ErrorCode ec);

  typedef void(Command::*BackendCallback)(std::shared_ptr<BackendConn> backend, ErrorCode ec);
  WriteReplyCallback WeakBind(BackendCallback mem_func, std::shared_ptr<BackendConn> backend) {
    std::weak_ptr<Command> cmd_wptr(shared_from_this());
    std::weak_ptr<BackendConn> backend_wptr(backend);
    return [cmd_wptr, mem_func, backend_wptr](ErrorCode ec) {
          if (auto cmd_ptr = cmd_wptr.lock()) {
            auto backend = backend_wptr.lock();
            ((*cmd_ptr).*mem_func)(backend, ec);
          }
        };
  }
  virtual void RotateReplyingBackend();
  virtual bool ParseReply(std::shared_ptr<BackendConn> backend);

private:
  static bool ParseRedisSimpleReply(std::shared_ptr<BackendConn> backend);
  static bool ParseMemcSimpleReply(std::shared_ptr<BackendConn> backend);

protected:
  std::shared_ptr<BackendConn> replying_backend_;
  std::shared_ptr<ClientConnection> client_conn_;
  bool has_written_some_reply_ = false;
  bool is_writing_reply_ = false;
private:
  ProtocolType protocol_;
};

}

#endif // _YARMPROXY_COMMAND_H_

