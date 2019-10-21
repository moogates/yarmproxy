#include "proxy_server.h"

#include "logging.h"

#include "backend_locator.h"
#include "client_conn.h"
#include "config.h"
#include "signal_watcher.h"
#include "worker_pool.h"

namespace yarmproxy {

static Endpoint ParseEndpoint(const std::string & ep) {
  size_t pos = ep.find(':');
  std::string host = ep.substr(0, pos);
  int port = std::stoi(ep.substr(pos + 1));
  return Endpoint(boost::asio::ip::address::from_string(host), port);
}

static size_t DefaultConcurrency() {
  size_t hd_concurrency = std::thread::hardware_concurrency();
  LOG_INFO << "ProxyServer hardware_concurrency " << hd_concurrency;
  return hd_concurrency == 0 ? 4 : hd_concurrency;
}

ProxyServer::ProxyServer(const std::string & addr, size_t worker_threads)
    : work_(io_service_)
    , acceptor_(io_service_)
    , listen_addr_(addr)
    , stopped_(false)
    , worker_pool_(new WorkerPool(
        worker_threads > 0 ? worker_threads : DefaultConcurrency())) {
}

ProxyServer::~ProxyServer() {
  if (!stopped_) {
    Stop();
  }
}

SignalHandler ProxyServer::WrapThreadSafeHandler(std::function<void()> handler) {
  boost::asio::io_service& ios(io_service_);
  return [&ios, handler](int) {
    ios.post(handler);
  };
}

void ProxyServer::Run() {
  std::shared_ptr<BackendLocator> locator(new BackendLocator());
  if (!locator->Initialize()) {
    LOG_ERROR << "ProxyServer BackendLocator Initialize error ...";
    return;
  }
  worker_pool_->OnLocatorUpdated(locator);

  auto endpoint = ParseEndpoint(listen_addr_);
  acceptor_.open(endpoint.protocol());
  acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
  acceptor_.bind(endpoint);

  boost::system::error_code ec;
  static const int BACKLOG = 1024; // TODO : config
  acceptor_.listen(BACKLOG, ec);
  if (ec) {
    LOG_ERROR << "ProxyServer listen error " << ec.message();
    return;
  }

  SignalWatcher::Instance().RegisterHandler(SIGHUP, WrapThreadSafeHandler([this]() {
      // FIXME : prevent signal handler reentrance
      if (!Config::Instance().ReloadCulsters()) {
        LOG_ERROR << "SIGHUP BackendLocator reload config error.";
        return;
      }
      std::shared_ptr<BackendLocator> locator(new BackendLocator());
      if (!locator->Initialize()) {
        LOG_ERROR << "SIGHUP BackendLocator reload Initialize error.";
        return;
      }
      LOG_WARN << "SIGHUP BackendLocator::Reload OK.";
      worker_pool_->OnLocatorUpdated(locator);
    }));
  SignalWatcher::Instance().RegisterHandler(SIGINT,
      WrapThreadSafeHandler([this]() {
        LOG_ERROR << "SIGINT Received.";
        Stop();
      }));
  SignalWatcher::Instance().RegisterHandler(SIGTERM,
      WrapThreadSafeHandler([this]() {
        LOG_ERROR << "SIGTERM Received.";
        Stop();
      }));

  worker_pool_->StartDispatching();
  StartAccept();

  while(!stopped_) {
  // try { // TODO : don't try for test
      io_service_.run();
      LOG_WARN << "ProxyServer io_service stopped.";
  //} catch (std::exception& e) {
  //  LOG_ERROR << "ProxyServer io_service.run error:" << e.what();
  //}
  }
}

void ProxyServer::Stop() {
  LOG_WARN << "ProxyServer Stop";
  stopped_ = true;
  io_service_.stop();
  worker_pool_->StopDispatching();
}

void ProxyServer::StartAccept() {
  WorkerContext& worker = worker_pool_->NextWorker();
  std::shared_ptr<ClientConnection> client_conn(new ClientConnection(worker));
  LOG_DEBUG << "ProxyServer create new conn, client=" << client_conn;

  acceptor_.async_accept(client_conn->socket(),
      std::bind(&ProxyServer::HandleAccept, this, client_conn,
                std::placeholders::_1));
}

void ProxyServer::HandleAccept(std::shared_ptr<ClientConnection> client_conn,
                               const boost::system::error_code& error) {
  if (!error) {
    client_conn->StartRead();
    StartAccept();
  } else {
    LOG_ERROR << "ProxyServer accept error!";
  }
}

}

