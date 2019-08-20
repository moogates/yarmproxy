#ifndef _WORKER_POOL_H_
#define _WORKER_POOL_H_

#include <boost/asio.hpp>

namespace mcproxy {
using namespace boost::asio;

class ClientConnection;
class BackendConnPool;

class WorkerPool {
public:
  explicit WorkerPool(size_t concurrency)
      : concurrency_(concurrency)
      , workers_(new Worker[concurrency])
      , next_worker_(0) { 
  }

  void StartDispatching();
  void StopDispatching();

  ClientConnection* NewClientConntion();
private:
  struct Worker {
    Worker();
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

}

#endif // _WORKER_POOL_H_

