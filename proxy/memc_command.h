#ifndef _MEMC_COMMAND_H_
#define _MEMC_COMMAND_H_

#include <list>
#include <vector>
#include <string>
#include <boost/asio.hpp>
#include <memory>

#include "client_conn.h"

using namespace boost::asio;

namespace mcproxy {

class BackendConn;
class ClientConnection;

class MemcCommand : public std::enable_shared_from_this<MemcCommand> {
public:
  static int CreateCommand(std::shared_ptr<ClientConnection> owner, const char* buf, size_t size,
                           std::shared_ptr<MemcCommand>* sub_cmds);
  MemcCommand(std::shared_ptr<ClientConnection> owner);

public:
  virtual ~MemcCommand();

  virtual void ForwardQuery(const char * data, size_t bytes) = 0;
  // backend_conn转发完毕ForwardQuery()指定的数据后，调用OnForwardQueryFinished()
  virtual void OnForwardQueryFinished(BackendConn* backend, const boost::system::error_code& error) = 0;

  // backend_conn收到reply数据后, 调用OnUpstreamReplyReceived()
  void OnUpstreamReplyReceived(BackendConn* backend, const boost::system::error_code& error);
  virtual void OnForwardReplyEnabled() = 0;
  void OnForwardReplyFinished(BackendConn* backend, const boost::system::error_code& error);

public:
  // void AsyncRead();
  void Abort();
  virtual std::string cmd_line_without_rn() const = 0; // for debug info only
  virtual size_t request_body_bytes() const {  // for debug info only
    return 0;
  }

private:
  virtual bool HasMoreBackend() const {
    return false;
  }
  virtual void RotateFirstBackend() {}

  void DeactivateReplyingBackend(BackendConn* backend) {
    // assert(backend == replying_backend_);
    if (backend == replying_backend_) {
      LOG_WARN << __func__ << " ok, backend=" << backend << " replying_backend_=" << replying_backend_;
    } else {
      LOG_WARN << __func__ << " error, backend=" << backend << " replying_backend_=" << replying_backend_;
    }
    replying_backend_ = nullptr;
  }
  bool TryActivateReplyingBackend(BackendConn* backend);

  virtual void DoForwardQuery(const char * data, size_t bytes) = 0;
  virtual bool ParseReply(BackendConn* backend) = 0;
  virtual size_t request_body_upcoming_bytes() const = 0;
protected:
  bool is_transfering_reply_;
  BackendConn* replying_backend_;

  std::shared_ptr<ClientConnection> client_conn_;

  WorkerContext& context_;

  void TryForwardReply(BackendConn* backend);
  virtual void PushReadyQueue(BackendConn* backend) {}

  typedef void(MemcCommand::*BackendCallbackFunc)(BackendConn* backend, const boost::system::error_code& error);
  ForwardReplyCallback WeakBind(BackendCallbackFunc mem_func, BackendConn* backend) {
    std::weak_ptr<MemcCommand> cmd_wptr(shared_from_this());
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

#endif // _MEMC_COMMAND_H_

