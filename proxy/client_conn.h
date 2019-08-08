#ifndef _CLIENT_CONN_H_
#define _CLIENT_CONN_H_

// #include "client_conn_fwd.h"

#include <queue>
#include <set>
#include <string>

#include <boost/asio.hpp>
#include <memory>

using namespace boost::asio;

namespace mcproxy {

class UpstreamConnPool;
class MemcCommand;

struct ResponseStatus {
  ResponseStatus() : unparsed_bytes(0), left_bytes(0), complete(false) {}

  void Reset() {
    unparsed_bytes = left_bytes = 0;
    complete = false;
  }
  
  // 当前 VALUE 块的未解析数据字节数(因为收到数据的长度不够, 暂不能解析)
  size_t unparsed_bytes;

  // 当前 VALUE 块的接收数据字节数(因为buffer的长度原因, 未能收到全部数据)
  size_t left_bytes;

  // 当前 MemcCommand 的数据已经全部收到(不只是某个VALUE块全收到)
  bool complete;
};

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

  void HandleForwardAllData(std::shared_ptr<MemcCommand> cmd, size_t bytes);
  void HandleForwardMoreData(std::shared_ptr<MemcCommand> cmd, size_t bytes);

  void OnCommandReady(std::shared_ptr<MemcCommand> memc_cmd);
  void OnCommandError(std::shared_ptr<MemcCommand> memc_cmd, const boost::system::error_code& error);

private:
  ResponseStatus response_status_;
protected:
  boost::asio::io_service& io_service_;
private:
  ip::tcp::socket socket_;

  enum {kBufLength = 64 * 1024};
  char up_buf_[kBufLength];
  size_t up_buf_begin_, up_buf_end_;

protected:
  UpstreamConnPool * upconn_pool_;

private:
  std::shared_ptr<MemcCommand> current_ready_cmd_;
  std::queue<std::shared_ptr<MemcCommand>> ready_cmd_queue_;
  std::set<std::shared_ptr<MemcCommand>> fetching_cmd_set_;
  // 当前命令被分拆成的子命令的个数
  size_t mapped_cmd_count_;

  size_t timeout_;
  boost::asio::deadline_timer timer_;

  void AsyncRead();
  void AsyncWrite();
  void AsyncWriteMissed();

  void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);
  void HandleWrite(const boost::system::error_code& error, size_t bytes_transferred);
  void HandleWriteMissed(const boost::system::error_code& error, size_t bytes_transferred);

  void HandleMemcCommandTimeout(const boost::system::error_code& error);
  void HandleTimeoutWrite(const boost::system::error_code& error);

  int MapMemcCommand(char * buf, size_t len);
};

}

#endif // _CLIENT_CONN_H_

