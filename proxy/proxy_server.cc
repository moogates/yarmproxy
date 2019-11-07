#include "proxy_server.h"

#include "logging.h"

#include "key_locator.h"
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
    : work_(io_context_)
    , acceptor_(io_context_)
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
  boost::asio::io_context& ios(io_context_);
  return [&ios, handler](int) {
    ios.post(handler);
  };
}

void ProxyServer::Run() {
  std::shared_ptr<KeyLocator> locator(new KeyLocator());
  if (!locator->Initialize()) {
    LOG_ERROR << "ProxyServer KeyLocator Initialize error ...";
    return;
  }
  worker_pool_->OnLocatorUpdated(locator);

  auto endpoint = ParseEndpoint(listen_addr_);
  acceptor_.open(endpoint.protocol());

  boost::system::error_code ec;

  boost::asio::ip::tcp::no_delay nodelay(true);
  acceptor_.set_option(nodelay, ec);

  acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
  acceptor_.bind(endpoint);

  // boost::system::error_code ec;
  static const int BACKLOG = 1024; // TODO : config
  acceptor_.listen(BACKLOG, ec);
  if (ec) {
    LOG_ERROR << "ProxyServer listen error " << ec.message();
    return;
  }

  SignalWatcher::Instance().RegisterHandler(SIGHUP, WrapThreadSafeHandler([this]() {
      // FIXME : prevent signal handler reentrance
      if (!Config::Instance().ReloadCulsters()) {
        LOG_ERROR << "SIGHUP KeyLocator reload config error.";
        return;
      }
      std::shared_ptr<KeyLocator> locator(new KeyLocator());
      if (!locator->Initialize()) {
        LOG_ERROR << "SIGHUP KeyLocator reload Initialize error.";
        return;
      }
      LOG_WARN << "SIGHUP KeyLocator::Reload OK.";
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
    try {
      io_context_.run(); // TODO : deprecated
      LOG_WARN << "ProxyServer io_context stopped.";
    } catch (std::exception& e) {
      LOG_ERROR << "ProxyServer io_context.run error:" << e.what();
    }
  }
}

void ProxyServer::Stop() {
  LOG_WARN << "ProxyServer Stop";
  stopped_ = true;
  io_context_.stop();
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

