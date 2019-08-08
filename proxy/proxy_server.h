#ifndef _PROXY_SERVER_H_
#define _PROXY_SERVER_H_

#include <memory>
#include <vector>
#include <boost/asio.hpp>

namespace mcproxy {

using namespace boost::asio;

class UpstreamConnPool;
class ClientConnection;

class RequestDispatcher {
public:
  explicit RequestDispatcher(int id);

  io_service& asio_service() {
    return io_service_;
  }

  UpstreamConnPool* upconn_pool() {
    return upconn_pool_;
  }

  void Run() {
    io_service_.run();
  }

  void Stop() {
    io_service_.stop();
  }

private:
  int id_;
  io_service io_service_;
  io_service::work work_;
  UpstreamConnPool* upconn_pool_;
};

class ProxyServer {
public:
  explicit ProxyServer(const ip::tcp::endpoint & ep);
  explicit ProxyServer(const std::string & addr);
  ~ProxyServer();

  ProxyServer(ProxyServer&) = delete;
  ProxyServer& operator=(ProxyServer&) = delete;

  void Run();

private:
  void StartAccept();
  void HandleAccept(std::shared_ptr<ClientConnection> conn, const boost::system::error_code& error);

  io_service io_service_;
  io_service::work work_;
  ip::tcp::acceptor acceptor_;

  size_t dispatch_threads_;

  size_t dispatch_round_;
  
  std::vector<RequestDispatcher*> dispatchers_;
};

}

#endif // _PROXY_SERVER_H_

