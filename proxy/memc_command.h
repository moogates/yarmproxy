#ifndef _MEMC_COMMAND_H_
#define _MEMC_COMMAND_H_

#include <list>
#include <vector>
#include <string>
#include <boost/asio.hpp>
#include <memory>

using namespace boost::asio;

namespace mcproxy {

class UpstreamConn;
class ClientConnection;

class MemcCommand : public std::enable_shared_from_this<MemcCommand> {
public:
  static int CreateCommand(boost::asio::io_service& io_service,
                           std::shared_ptr<ClientConnection> owner, const char* buf, size_t size,
                           std::list<std::shared_ptr<MemcCommand>>* sub_cmds);
  MemcCommand(boost::asio::io_service& io_service, const ip::tcp::endpoint & ep, 
      std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len);

public:
  virtual ~MemcCommand();
//////////////////////////////////////
  void ForwardRequest(const char * buf, size_t bytes);
  virtual bool ParseUpstreamResponse() = 0;

  virtual void OnUpstreamRequestWritten(size_t bytes, const boost::system::error_code& error) {
  }

  void OnForwardResponseFinished(size_t bytes, const boost::system::error_code& error);

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
  bool upstream_nomore_response() {
    return upstream_nomore_response_;
  }
  void set_upstream_nomore_response() {
    upstream_nomore_response_ = true;
  }
//////////////////////////////////////
  // void AsyncRead();
  void Abort();
public:
  virtual std::string cmd_line_without_rn() const = 0; // for debug info only
  virtual size_t request_body_bytes() const {  // for debug info only
    return 0;
  }

public:
  UpstreamConn * upstream_conn() {
    return upstream_conn_;
  }

  void set_upstream_conn(UpstreamConn * conn) {
    upstream_conn_ = conn;
  }
protected:
  bool is_forwarding_response_;

protected:
  ip::tcp::endpoint upstream_endpoint_;
protected:
  UpstreamConn * upstream_conn_;

  std::shared_ptr<ClientConnection> client_conn_;
  boost::asio::io_service& io_service_;

private:

  timeval time_created_;
  bool loaded_;
////////////////////////////
  bool upstream_nomore_response_;
};

}

#endif // _MEMC_COMMAND_H_

