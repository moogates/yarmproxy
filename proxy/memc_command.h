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
                           std::list<std::shared_ptr<MemcCommand>>* sub_cmds);
  MemcCommand(const ip::tcp::endpoint & ep, std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len);

public:
  virtual ~MemcCommand();
//////////////////////////////////////
  void ForwardRequest(const char * buf, size_t bytes);
  virtual bool ParseUpstreamResponse() = 0;

  virtual void OnUpstreamRequestWritten(const boost::system::error_code& error) {
  }

  virtual void OnForwardResponseReady() {}
  void OnUpstreamResponse(const boost::system::error_code& error);
private:
  // 判断是否最靠前的command, 是才可以转发
  bool IsFormostCommand();
  virtual void DoForwardRequest(const char * buf, size_t bytes) = 0;
  virtual size_t request_body_upcoming_bytes() const {
    return 0;
  }

public:
  bool backend_nomore_response();
  // void AsyncRead();
  void Abort();
public:
  virtual std::string cmd_line_without_rn() const = 0; // for debug info only
  virtual size_t request_body_bytes() const {  // for debug info only
    return 0;
  }

public:
  BackendConn * backend_conn() {
    return backend_conn_;
  }
  void OnForwardResponseFinished(const boost::system::error_code& error);

protected:
  bool is_forwarding_response_;

protected:
  ip::tcp::endpoint backend_endpoint_;
protected:
  BackendConn * backend_conn_;

  std::shared_ptr<ClientConnection> client_conn_;
  WorkerContext& context_;

  template<class MemFun>
  ForwardResponseCallback WeakBind(MemFun&& mf) { // TODO : refine it!
    std::weak_ptr<MemcCommand> cmd_wptr(shared_from_this());
    return [cmd_wptr, mf](const boost::system::error_code& error) {
          if (auto cmd_ptr = cmd_wptr.lock()) {
            ((*cmd_ptr).*mf)(error);
          }
        };
  }

private:
  timeval time_created_;
  bool loaded_;
};

}

#endif // _MEMC_COMMAND_H_

