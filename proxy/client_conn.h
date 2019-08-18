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

  void recursive_lock_buffer();
  void recursive_unlock_buffer();

public:
  void ForwardResponse(const char* data, size_t bytes, const ForwardResponseCallback& cb);
  bool IsFirstCommand(std::shared_ptr<MemcCommand> cmd) {
    return cmd == poly_cmd_queue_.front();
  }
  void RotateFirstCommand();
  void TryReadMoreRequest();
private:

public:
  void update_processed_bytes(size_t transfered);
  void try_free_buffer_space();

  bool has_much_free_space() {
    return received_bytes_ * 3 <  BUFFER_SIZE * 2; // there is still more than 1/3 buffer space free
  }

  char* free_space_begin() {
    return data_ + received_bytes_;
  }
  size_t free_space_size() {
    return BUFFER_SIZE - received_bytes_;
  }

  const char* unprocessed_data() const {
    return data_ + processed_bytes_;
  }

  size_t parsed_unreceived_bytes() const {
    if (parsed_bytes_ > received_bytes_) {
      return parsed_bytes_ - received_bytes_;
    }
    return 0;
  }

  size_t received_bytes() const {
    return received_bytes_ - processed_bytes_;
  }

  size_t unparsed_received_bytes() const {
    if (received_bytes_ > parsed_bytes_) {
      return received_bytes_ - parsed_bytes_;
    }
    return 0;
  }
//size_t unprocessed_bytes() const {
//  return std::min(received_bytes_, parsed_bytes_) - processed_bytes_;
//}

protected:
  boost::asio::io_service& io_service_;
private:
  ip::tcp::socket socket_;

  enum {BUFFER_SIZE = 64 * 1024};
  char data_[BUFFER_SIZE];
  size_t buf_lock_;
  size_t processed_bytes_, received_bytes_;
  size_t parsed_bytes_;

protected:
  UpstreamConnPool * upconn_pool_;

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

