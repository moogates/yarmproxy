#ifndef _YARMPROXY_COMMAND_H_
#define _YARMPROXY_COMMAND_H_

#include <list>
#include <vector>
#include <string>
#include <memory>

#include <boost/asio.hpp>

using namespace boost::asio;

namespace yarmproxy {

class BackendConn;
class WorkerContext;
class ClientConnection;

enum ErrorCode {
  E_SUCCESS  = 0,
  E_CONNECT  = 2,
  E_READ     = 3,
  E_WRITE    = 4,
  E_PROTOCOL = 5,
  E_OTHERS   = 100,
};

typedef std::function<void(const boost::system::error_code& error)> ForwardReplyCallback;

class Command : public std::enable_shared_from_this<Command> {
public:
  static int CreateCommand(std::shared_ptr<ClientConnection> client, const char* buf, size_t size,
                           std::shared_ptr<Command>* cmd);
  Command(std::shared_ptr<ClientConnection> client);

public:
  virtual ~Command();

  virtual void ForwardQuery(const char * data, size_t bytes) = 0;

  // backend_conn转发完毕ForwardQuery()指定的数据后，调用OnForwardQueryFinished()
  virtual void OnForwardQueryFinished(BackendConn* backend, const boost::system::error_code& error) = 0;

  // backend_conn收到reply数据后, 调用OnUpstreamReplyReceived()
  void OnUpstreamReplyReceived(BackendConn* backend, const boost::system::error_code& error);
  virtual void OnForwardReplyEnabled() = 0;
  void OnForwardReplyFinished(BackendConn* backend, const boost::system::error_code& error);

public:
  // void AsyncRead();
  void ErrorSilence();
  void ErrorReport();
  void ErrorAbort();

private:
  virtual void HookOnUpstreamReplyReceived(BackendConn* backend){}
  virtual void RotateReplyingBackend();

  bool TryActivateReplyingBackend(BackendConn* backend);

  virtual void DoForwardQuery(const char * data, size_t bytes) = 0;
  virtual bool ParseReply(BackendConn* backend) = 0;
  virtual size_t query_body_upcoming_bytes() const = 0;
protected:
  bool is_transfering_reply_;
  BackendConn* replying_backend_;
  size_t completed_backends_;

  std::shared_ptr<ClientConnection> client_conn_;

  WorkerContext& context();

  void TryForwardReply(BackendConn* backend);
  virtual void PushWaitingReplyQueue(BackendConn* backend) {}

  typedef void(Command::*BackendCallbackFunc)(BackendConn* backend, const boost::system::error_code& error);
  ForwardReplyCallback WeakBind(BackendCallbackFunc mem_func, BackendConn* backend) {
    std::weak_ptr<Command> cmd_wptr(shared_from_this());
    return [cmd_wptr, mem_func, backend](const boost::system::error_code& error) {
          if (auto cmd_ptr = cmd_wptr.lock()) {
            ((*cmd_ptr).*mem_func)(backend, error);
          }
        };
  }

private:
  timeval time_created_;
  bool loaded_;
};

}

#endif // _YARMPROXY_COMMAND_H_

