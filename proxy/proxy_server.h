#ifndef _PROXY_SERVER_H_
#define _PROXY_SERVER_H_

#include <memory>
#include <vector>
#include <boost/asio.hpp>
#include "base/logging.h"

namespace mcproxy {

using namespace boost::asio;

class BackendConnPool;
class ClientConnection;

class WorkerPool {
public:
  explicit WorkerPool(size_t concurrency)
      : concurrency_(concurrency)
      , workers_(new Worker[concurrency])
      , next_worker_(0) { 
  }

  ClientConnection* NewClientConntion();

  void StartDispatching() {
    for(size_t i = 0; i < concurrency_; ++i) {
      Worker& woker = workers_[i];
      std::thread th([&woker]() {
            try {
              woker.io_service_.run();
            } catch (std::exception& e) {
              LOG_ERROR << "WorkerThread io_service.run error:" << e.what();
            }
          });
      th.detach();
      woker.thread_ = std::move(th); // what to to with the socket?
    }
  }

  void StopDispatching() {
    for(size_t i = 0; i < concurrency_; ++i) {
      workers_[i].io_service_.stop();
    }
  }
private:
  struct Worker {
    Worker();
  //Worker() : work_(io_service_), upconn_pool_(new BackendConnPool(io_service_)) {
  //}
    std::thread thread_;
    io_service io_service_;
    io_service::work work_;
    BackendConnPool* upconn_pool_;
  };

  size_t concurrency_;
  Worker* workers_; // TODO : use std::unique_ptr

  Worker& NextWorker() {
    return workers_[next_worker_++ % concurrency_];
  }
  size_t next_worker_;
};

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
  std::thread thread_;
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

