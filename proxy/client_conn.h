#ifndef _CLIENT_CONNECTION_H_
#define _CLIENT_CONNECTION_H_

#include <list>
#include <queue>
#include <set>
#include <string>

#include <boost/asio.hpp>
#include <memory>

using namespace boost::asio;

namespace mcproxy {

class UpstreamConnPool;
class MemcCommand;

typedef std::function<void(const boost::system::error_code& error)> ForwardResponseCallback;

class ClientConnection : public std::enable_shared_from_this<ClientConnection> 
{
public:
  ClientConnection(boost::asio::io_service& io_service, UpstreamConnPool * pool);
  ~ClientConnection();

  ip::tcp::socket& socket() {
    return socket_;
  }

  UpstreamConnPool * upconn_pool() {
    return upconn_pool_;
  }

  void Start();

  void OnCommandError(std::shared_ptr<MemcCommand> memc_cmd, const boost::system::error_code& error);

  void update_processed_bytes(size_t transfered);
  void recursive_lock_buffer();
  void recursive_unlock_buffer();
private:
  void try_free_buffer_space();

protected:
  boost::asio::io_service& io_service_;
private:
  ip::tcp::socket socket_;

  enum {kBufLength = 64 * 1024};
  char buf_[kBufLength];
  size_t buf_lock_;
  size_t up_buf_begin_, up_buf_end_;
  size_t parsed_bytes_;

  std::shared_ptr<MemcCommand> cmd_need_more_data_; // use weak ptr

protected:
  UpstreamConnPool * upconn_pool_;

public:
  void ForwardResponse(const char* data, size_t bytes, const ForwardResponseCallback& cb);
  bool IsFirstCommand(std::shared_ptr<MemcCommand> cmd) {
    return cmd == poly_cmd_queue_.front();
  }
  void RotateFirstCommand();
  void TryReadMoreRequest();

private:
  ForwardResponseCallback forward_resp_callback_;

  std::shared_ptr<MemcCommand> current_ready_cmd_;
  std::queue<std::shared_ptr<MemcCommand>> ready_cmd_queue_;
  std::set<std::shared_ptr<MemcCommand>> fetching_cmd_set_;

  std::list<std::shared_ptr<MemcCommand>> poly_cmd_queue_; // 新版支持多态的cmd

  size_t timeout_;
  boost::asio::deadline_timer timer_;

  void AsyncRead();

  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);

  void HandleMemcCommandTimeout(const boost::system::error_code& error);
  void HandleTimeoutWrite(const boost::system::error_code& error);
};

}

#endif // _CLIENT_CONNECTION_H_

