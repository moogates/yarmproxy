#ifndef _YARMPROXY_PROXY_SERVER_H_
#define _YARMPROXY_PROXY_SERVER_H_

#include <memory>
#include <vector>
#include <boost/asio.hpp>

namespace yarmproxy {

class ClientConnection;
class WorkerPool;

class ProxyServer {
public:
  explicit ProxyServer(const std::string & addr, size_t worker_threads);
  ~ProxyServer();

  void Run();
  void Stop();

private:
  ProxyServer(ProxyServer&) = delete;
  ProxyServer& operator=(ProxyServer&) = delete;

  void StartAccept();
  void HandleAccept(std::shared_ptr<ClientConnection> conn, const boost::system::error_code& error);

private:
  boost::asio::io_service io_service_;
  boost::asio::io_service::work work_;
  boost::asio::ip::tcp::acceptor acceptor_;
  std::string listen_addr_;
  bool stopped_;

  std::unique_ptr<WorkerPool> worker_pool_;
};

}

#endif // _YARMPROXY_PROXY_SERVER_H_

