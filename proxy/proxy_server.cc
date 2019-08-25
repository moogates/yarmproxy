#include "proxy_server.h"

#include <thread>
#include <functional>

#include "base/logging.h"

#include "client_conn.h"
#include "worker_pool.h"
#include "memcached_locator.h"

namespace mcproxy {

static ip::tcp::endpoint ParseEndpoint(const std::string & ep) {
  size_t pos = ep.find(':');
  std::string host = ep.substr(0, pos);
  int port = std::stoi(ep.substr(pos + 1));
  return ip::tcp::endpoint(ip::address::from_string(host), port);
}

ProxyServer::ProxyServer(const std::string & addr, size_t worker_concurrency)
  : work_(io_service_)
  , acceptor_(io_service_, ParseEndpoint(addr))
  , worker_pool_(new WorkerPool(worker_concurrency)) {
}

ProxyServer::~ProxyServer() {
  io_service_.stop();
  worker_pool_->StopDispatching();
}

void ProxyServer::Run() {
  if (!MemcachedLocator::Instance().Initialize()) {
    LOG_ERROR << "MemcachedLocator initialization error ...";
    return;
  }

  worker_pool_->StartDispatching();
  StartAccept();

  try {
    io_service_.run();
  } catch (std::exception& e) {
    LOG_ERROR << "ProxyServer io_service.run error:" << e.what();
  }
}

void ProxyServer::StartAccept() {
  std::shared_ptr<ClientConnection> client_conn(worker_pool_->NewClientConntion());

  acceptor_.async_accept(client_conn->socket(),
      std::bind(&ProxyServer::HandleAccept, this, client_conn,
        std::placeholders::_1));
}

void ProxyServer::HandleAccept(std::shared_ptr<ClientConnection> client_conn, const boost::system::error_code& error) {
  if (!error) {
    client_conn->StartRead();
    StartAccept();
  } else {
    LOG_ERROR << "ProxyServer accept error!";
  }
}

}

