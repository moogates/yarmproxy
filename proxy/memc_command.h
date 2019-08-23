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

  virtual void ForwardRequest(const char * data, size_t bytes) = 0;
  // backend_conn转发完毕ForwardRequest()指定的数据后，如果发现当前command请求还有更多数据，则调用OnForwardMoreRequest()继续转发
  virtual void OnForwardMoreRequest(const boost::system::error_code& error) {}

  // backend_conn收到reply数据后, 调用OnUpstreamResponseReceived()
  void OnUpstreamResponseReceived(BackendConn* backend, const boost::system::error_code& error);
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
    assert(backend == replying_backend_);
    replying_backend_ = nullptr;
  }
  bool TryActivateReplyingBackend(BackendConn* backend) {
    if (backend == replying_backend_) {
      return true;
    }
    if (replying_backend_ == nullptr) {
      replying_backend_ = backend;
      return true;
    }
    return false;
  }

  // 判断是否最靠前的command, 是才可以转发
  bool IsFormostCommand();
  virtual void DoForwardRequest(const char * data, size_t bytes) = 0;
  virtual bool ParseUpstreamResponse(BackendConn* backend) = 0;
  virtual size_t request_body_upcoming_bytes() const = 0;
protected:
  bool is_transfering_response_;
  BackendConn* replying_backend_;

  std::shared_ptr<ClientConnection> client_conn_;
  WorkerContext& context_;

  void TryForwardResponse(BackendConn* backend);
  virtual void PushReadyQueue(BackendConn* backend) {}

  typedef void(MemcCommand::*FuncType)(const boost::system::error_code& error);
  ForwardResponseCallback WeakBind(FuncType mf) {
    std::weak_ptr<MemcCommand> cmd_wptr(shared_from_this());
    return [cmd_wptr, mf](const boost::system::error_code& error) {
          if (auto cmd_ptr = cmd_wptr.lock()) {
            ((*cmd_ptr).*mf)(error);
          }
        };
  }

  typedef void(MemcCommand::*FuncType2)(BackendConn* backend, const boost::system::error_code& error);
  ForwardResponseCallback WeakBind2(FuncType2 mf, BackendConn* backend) {
    std::weak_ptr<MemcCommand> cmd_wptr(shared_from_this());
    return [cmd_wptr, mf, backend](const boost::system::error_code& error) {
          if (auto cmd_ptr = cmd_wptr.lock()) {
            ((*cmd_ptr).*mf)(backend, error);
          }
        };
  }

private:
  timeval time_created_;
  bool loaded_;
};

}

#endif // _MEMC_COMMAND_H_

