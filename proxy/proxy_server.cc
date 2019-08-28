#include "proxy_server.h"

// #include <thread>
// #include <functional>

#include "logging.h"

#include "client_conn.h"
#include "worker_pool.h"
#include "backend_locator.h"

namespace yarmproxy {

static ip::tcp::endpoint ParseEndpoint(const std::string & ep) {
  size_t pos = ep.find(':');
  std::string host = ep.substr(0, pos);
  int port = std::stoi(ep.substr(pos + 1));
  return ip::tcp::endpoint(ip::address::from_string(host), port);
}

static size_t DefaultConcurrency() {
  size_t hd_concurrency = std::thread::hardware_concurrency();
  LOG_INFO << "ProxyServer hardware_concurrency " << hd_concurrency;
  return hd_concurrency == 0 ? 4 : hd_concurrency;
}

ProxyServer::ProxyServer(const std::string & addr, size_t concurrency)
    : work_(io_service_)
    , acceptor_(io_service_, ParseEndpoint(addr))
    , worker_pool_(new WorkerPool(concurrency > 0 ? concurrency : DefaultConcurrency())) {
}

ProxyServer::~ProxyServer() {
  io_service_.stop();
  worker_pool_->StopDispatching();
}

void ProxyServer::Run() {
  if (!BackendLoactor::Instance().Initialize()) {
    LOG_ERROR << "BackendLoactor initialization error ...";
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
  WorkerContext& worker = worker_pool_->NextWorker();
  std::shared_ptr<ClientConnection> client_conn(new ClientConnection(worker));

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

