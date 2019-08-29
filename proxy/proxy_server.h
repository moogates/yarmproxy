#ifndef _YARMPROXY_PROXY_SERVER_H_
#define _YARMPROXY_PROXY_SERVER_H_

#include <memory>
#include <vector>
#include <boost/asio.hpp>

namespace yarmproxy {

using namespace boost::asio;

class ClientConnection;
class WorkerPool;

class ProxyServer {
public:
  explicit ProxyServer(const std::string & addr, size_t worker_concurrency = 0);
  ~ProxyServer();

  void Run();

private:
  ProxyServer(ProxyServer&) = delete;
  ProxyServer& operator=(ProxyServer&) = delete;

  void StartAccept();
  void HandleAccept(std::shared_ptr<ClientConnection> conn, const boost::system::error_code& error);

private:
  io_service io_service_;
  io_service::work work_;
  ip::tcp::acceptor acceptor_;

  std::unique_ptr<WorkerPool> worker_pool_;
};

}

#endif // _YARMPROXY_PROXY_SERVER_H_

