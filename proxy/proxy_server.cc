#include "proxy_server.h"

#include <thread>
#include <functional>

#include "base/logging.h"

#include "client_conn.h"
#include "memcached_locator.h"
#include "upstream_conn.h"

namespace mcproxy {

RequestDispatcher::RequestDispatcher(int id) : id_(id), work_(io_service_) {
  upconn_pool_ = new UpstreamConnPool(io_service_); // TODO : 
  // upconn_pool_ = new UpstreamConnPool(); // TODO : 
}

ClientConnection* RequestDispatcher::NewClientConntion() {
  return new ClientConnection(io_service_, upconn_pool_);
}

static ip::tcp::endpoint ParseEndpoint(const std::string & ep) {
  size_t pos = ep.find_first_of(':');
  std::string host = ep.substr(0, pos);
  int port = std::stoi(ep.substr(pos + 1));
  return ip::tcp::endpoint(ip::address::from_string(host), port);
}

ProxyServer::ProxyServer(const std::string & addr)
  : work_(io_service_)
  , acceptor_(io_service_, ParseEndpoint(addr))
  , dispatch_threads_(4)
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

RequestDispatcher* ProxyServer::NextDispatcher() {
  RequestDispatcher* dispatcher = dispatchers_[dispatch_round_++];
  if(++dispatch_round_ >= dispatch_threads_) {
    dispatch_round_ = 0;
  }
  return dispatcher;
}

void ProxyServer::StartAccept() {
  LOG_DEBUG << "ClientConnection created, dispatcher=" << dispatch_round_;
  std::shared_ptr<ClientConnection> client_conn(NextDispatcher()->NewClientConntion());

  acceptor_.async_accept(client_conn->socket(),
      std::bind(&ProxyServer::HandleAccept, this, client_conn,
        std::placeholders::_1));
}

void ProxyServer::HandleAccept(std::shared_ptr<ClientConnection> client_conn, const boost::system::error_code& error) {
  if (!error) {
    client_conn->Start();
    StartAccept();
  } else {
    LOG_WARN << "io_service accept error!";
  }
}

}

