#ifndef _MEMC_COMMAND_H_
#define _MEMC_COMMAND_H_

// #include "memc_command_fwd.h"

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
                           size_t* cmd_line_bytes, size_t* body_bytes, bool* lock_buffer, std::list<std::shared_ptr<MemcCommand>>* sub_cmds);

  MemcCommand(boost::asio::io_service& io_service, const ip::tcp::endpoint & ep, 
      std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len);

  virtual ~MemcCommand();
//////////////////////////////////////
  virtual void ForwardRequest(const char * buf, size_t bytes);
  virtual void OnUpstreamResponse(const boost::system::error_code& error) {
  }
  virtual void OnUpstreamRequestWritten(size_t bytes, const boost::system::error_code& error) {
  }

  virtual void OnForwardResponseFinished(size_t bytes, const boost::system::error_code& error) {
  }
  virtual void OnForwardResponseReady() {}

  // 判断是否最靠前的command, 是才可以转发
  virtual bool IsFormostCommand() {
    return false;
  }

  virtual size_t upcoming_bytes() const {
    return 0;
  }
  bool upstream_nomore_data() {
    return upstream_nomore_data_;
  }
  void set_upstream_nomore_data() {
    upstream_nomore_data_ = true;
  }
//////////////////////////////////////
  void AsyncRead();
  void Abort();

  // response_cmd_line
  const std::string & cmd_line() const {
    return cmd_line_;
  }
  size_t cmd_line_bytes() const {
    return cmd_line_.size();
  }
  bool cmd_line_forwarded() {
    return cmd_line_forwarded_;
  }
  void set_cmd_line_forwarded(bool b) {
    cmd_line_forwarded_ = b;
  }

  size_t body_bytes() const {
    return body_bytes_;
  }
  size_t total_bytes() const {
    return cmd_line_bytes() + body_bytes();
  }
  size_t forwarded_bytes() const {
    return forwarded_bytes_;
  }
  size_t missed_popped_bytes() const {
    return missed_popped_bytes_;
  }
  void set_missed_popped_bytes(size_t bytes) {
    missed_popped_bytes_ = bytes;
  }

  const std::string & method() const {
    return method_;
  }

  std::string & missed_buf() {
    return missed_buf_;
  }

  bool missed_ready() const {
    return missed_ready_;
  }

  const ip::tcp::endpoint & upstream_endpoint() const {
    return upstream_endpoint_;
  }

  UpstreamConn * upstream_conn() {
    return upstream_conn_;
  }

  bool NeedLoadMissed();

  void set_upstream_conn(UpstreamConn * conn) {
    upstream_conn_ = conn;
  }

  void RemoveMissedKey(const std::string & key);
  void LoadMissedKeys();
  void DispatchMissedKeyData();
public: // TODO : should be private
  void HandleConnect(const char * buf, size_t bytes, const boost::system::error_code& error);
  void HandleWrite(const char * buf, const size_t bytes, const boost::system::error_code& error, size_t bytes_transferred);
  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);
  void HandleMissedKeyReady();
protected:
  std::string cmd_line_;
  std::string method_;

  bool cmd_line_forwarded_;
  size_t forwarded_bytes_;
  size_t body_bytes_;

  std::vector<std::string> missed_keys_;
  bool missed_ready_;
  std::string missed_buf_;
  size_t missed_popped_bytes_;
  boost::asio::deadline_timer * missed_timer_;

  ip::tcp::endpoint upstream_endpoint_;
  UpstreamConn * upstream_conn_;
protected:
  std::shared_ptr<ClientConnection> client_conn_;
  boost::asio::io_service& io_service_;

private:

  timeval time_created_;
  bool loaded_;
////////////////////////////
  bool upstream_nomore_data_;
};

}

#endif // _MEMC_COMMAND_H_

