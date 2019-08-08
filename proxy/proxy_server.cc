#include "proxy_server.h"

#include <thread>
#include <functional>

#include "base/logging.h"

#include "client_conn.h"
#include "memcached_locator.h"
#include "upstream_conn.h"

namespace mcproxy {

RequestDispatcher::RequestDispatcher(int id) : id_(id), work_(io_service_) {
  upconn_pool_ = new UpstreamConnPool(); // TODO : 
}

static ip::tcp::endpoint ParseEndpoint(const std::string & ep) {
  size_t pos = ep.find_first_of(':');
  std::string host = ep.substr(0, pos);
  int port = std::stoi(ep.substr(pos + 1));
  return ip::tcp::endpoint(ip::address::from_string(host), port);
}


ProxyServer::ProxyServer(const ip::tcp::endpoint & ep)
  : work_(io_service_)
  , acceptor_(io_service_, ep)
  , dispatch_threads_(32)
  , dispatch_round_(0)
{
}

ProxyServer::ProxyServer(const std::string & addr)
  : work_(io_service_)
  , acceptor_(io_service_, ParseEndpoint(addr))
  , dispatch_threads_(32)
  , dispatch_round_(0)
{
}

ProxyServer::~ProxyServer() {
  // TODO : 细化 asio 的这些调用
  io_service_.stop();
  for(size_t i = 0; i < dispatch_threads_; ++i) {
  //dispatch_services_[i]->stop();
  //delete dispatch_services_[i];

    dispatchers_[i]->Stop();
    delete dispatchers_[i];
  }
}

void ProxyServer::Run() {
  if (!MemcachedLocator::Instance().Initialize()) {
    LOG_WARN << "MemcachedLocator initialization error ...";
    return;
  }

  io_service_.reset();

  for(size_t i = 0; i < dispatch_threads_; ++i) {
    dispatchers_.push_back(new RequestDispatcher(i));
  }

  for(size_t i = 0; i < dispatch_threads_; ++i) {
    std::thread th([this, i]() { dispatchers_[i]->Run(); } );
    th.detach();
  }

  StartAccept();

  try {
    io_service_.run();
  } catch (std::exception& e) {
    LOG_WARN << "io_service.run error : " << e.what();
  }
}

void ProxyServer::StartAccept() {
  std::shared_ptr<ClientConnection> conn(new ClientConnection(dispatchers_[dispatch_round_]->asio_service(),
                                                              dispatchers_[dispatch_round_]->upconn_pool()));
  if(++dispatch_round_ >= dispatch_threads_) {
    dispatch_round_ = 0;
  }

  LOG_DEBUG << "ClientConnection created, dispatcher=";

  acceptor_.async_accept(conn->socket(),
      std::bind(&ProxyServer::HandleAccept, this, conn,
        std::placeholders::_1));
}

void ProxyServer::HandleAccept(std::shared_ptr<ClientConnection> conn, const boost::system::error_code& error) {
  if (!error) {
    conn->Start();
    StartAccept();
  } else {
    LOG_WARN << "io_service accept error!";
  }
}

}

