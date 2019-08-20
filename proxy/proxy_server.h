#ifndef _PROXY_SERVER_H_
#define _PROXY_SERVER_H_

#include <memory>
#include <vector>
#include <boost/asio.hpp>

namespace mcproxy {

using namespace boost::asio;

class BackendConnPool;
class ClientConnection;

class RequestDispatcher {
public:
  explicit RequestDispatcher(int id);
  ClientConnection* NewClientConntion();

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
  BackendConnPool* upconn_pool_;
};

class ProxyServer {
public:
  explicit ProxyServer(const std::string & addr);
  ~ProxyServer();

  void Run();

private:
  ProxyServer(ProxyServer&) = delete;
  ProxyServer& operator=(ProxyServer&) = delete;

  void StartAccept();
  void HandleAccept(std::shared_ptr<ClientConnection> conn, const boost::system::error_code& error);

  io_service io_service_;
  io_service::work work_;
  ip::tcp::acceptor acceptor_;
  
  RequestDispatcher* NextDispatcher();
  size_t dispatch_threads_;
  size_t dispatch_round_;
  std::vector<RequestDispatcher*> dispatchers_;
};

}

#endif // _PROXY_SERVER_H_

