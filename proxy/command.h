#ifndef _YARMPROXY_COMMAND_H_
#define _YARMPROXY_COMMAND_H_

#include <list>
#include <vector>
#include <string>
#include <memory>

#include <boost/asio.hpp>
using namespace boost::asio; // TODO : minimize endpoint dependency

namespace yarmproxy {

class BackendConn;
class BackendConnPool;
class ClientConnection;

enum class ErrorCode;

typedef std::function<void(ErrorCode ec)> WriteReplyCallback;

class Command : public std::enable_shared_from_this<Command> {
public:
  static int CreateCommand(std::shared_ptr<ClientConnection> client,
                           const char* buf, size_t size,
                           std::shared_ptr<Command>* cmd);
protected: // TODO : best practice ?
  Command(std::shared_ptr<ClientConnection> client);
public:
  virtual ~Command();
  virtual void WriteQuery() = 0;
  virtual void OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) = 0;
  virtual void StartWriteReply() = 0;

  virtual void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec);
  void OnWriteReplyFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec);

  virtual bool QueryParsingComplete() {
    return true;
  }
  virtual bool ParseIncompleteQuery() { return true; }

protected:
  BackendConnPool* backend_pool();
  std::shared_ptr<BackendConn> AllocateBackend(const ip::tcp::endpoint& ep);
  void TryWriteReply(std::shared_ptr<BackendConn> backend);

  virtual void OnBackendConnectError(std::shared_ptr<BackendConn> backend);

  typedef void(Command::*BackendCallback)(std::shared_ptr<BackendConn> backend, ErrorCode ec);
  WriteReplyCallback WeakBind(BackendCallback mem_func, std::shared_ptr<BackendConn> backend) {
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
  virtual bool query_data_zero_copy() = 0;

  virtual void RotateReplyingBackend(bool success) = 0;
  virtual bool ParseReply(std::shared_ptr<BackendConn> backend) = 0;

protected:
  std::shared_ptr<ClientConnection> client_conn_;
private:
  bool is_transfering_reply_;
};

}

#endif // _YARMPROXY_COMMAND_H_

