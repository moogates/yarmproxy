#include "proxy_server.h"

#include <iostream>
// #include <functional>

#include "logging.h"

#include "client_conn.h"
#include "worker_pool.h"
#include "backend_locator.h"
#include "signal_watcher.h"

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
    , acceptor_(io_service_)
    , listen_addr_(addr)
    , stopped_(false)
    , worker_pool_(new WorkerPool(concurrency > 0 ? concurrency : DefaultConcurrency())) {
}

ProxyServer::~ProxyServer() {
  if (!stopped_) {
    Stop();
  }
}

void ProxyServer::Run() {
  if (!BackendLoactor::Instance().Initialize()) {
    LOG_ERROR << "ProxyServer BackendLoactor init error ...";
    return;
  }

  auto endpoint = ParseEndpoint(listen_addr_);
  acceptor_.open(endpoint.protocol());
  acceptor_.set_option(ip::tcp::acceptor::reuse_address(true));
  acceptor_.bind(endpoint);

  boost::system::error_code ec;
  static const int BACKLOG = 1024; // TODO : config
  acceptor_.listen(BACKLOG, ec);
  if (ec) {
    LOG_ERROR << "BackendLoactor listen error " << ec.message();
    return;
  }

  SignalWatcher::Instance().RegisterHandler(SIGHUP, [this](int) { Stop(); });
  SignalWatcher::Instance().RegisterHandler(SIGINT, [this](int) { Stop(); });

  worker_pool_->StartDispatching();
  StartAccept();

  while(!stopped_) {
    try {
      std::cout << "ProxyServer io_service.run begin." << std::endl;
      io_service_.run();
      std::cout << "ProxyServer io_service.run end." << std::endl;
    } catch (std::exception& e) {
      LOG_ERROR << "ProxyServer io_service.run error:" << e.what();
    }
  }
}

void ProxyServer::Stop() {
  std::cout << "ProxyServer Stop." << std::endl;
  stopped_ = true;
  io_service_.stop();
  std::cout << "ProxyServer Stop 2." << std::endl;
  worker_pool_->StopDispatching();
}

void ProxyServer::StartAccept() {
  WorkerContext& worker = worker_pool_->NextWorker();
  std::shared_ptr<ClientConnection> client_conn(new ClientConnection(worker));
  LOG_DEBUG << "ProxyServer create new conn, client=" << client_conn.get();

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

