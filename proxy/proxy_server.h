#ifndef _YARMPROXY_PROXY_SERVER_H_
#define _YARMPROXY_PROXY_SERVER_H_

#include <memory>
#include <vector>
#include <boost/asio.hpp>

namespace yarmproxy {

class ClientConnection;
class WorkerPool;

using SignalHandler = std::function<void(int sigid)>;

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
  boost::asio::io_context io_context_;
  boost::asio::io_context::work work_;
  boost::asio::ip::tcp::acceptor acceptor_;
  std::string listen_addr_;
  bool stopped_;

  std::unique_ptr<WorkerPool> worker_pool_;

  // SignalHandler WrapThreadSafeHandler(SignalHandler handler);
  SignalHandler WrapThreadSafeHandler(std::function<void()> handler);
};

}

#endif // _YARMPROXY_PROXY_SERVER_H_

