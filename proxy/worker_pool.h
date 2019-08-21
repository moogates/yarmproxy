#ifndef _WORKER_POOL_H_
#define _WORKER_POOL_H_

#include <boost/asio.hpp>

namespace mcproxy {
using namespace boost::asio;

class ClientConnection;
class BackendConnPool;

struct WorkerContext {
  WorkerContext();
  std::thread thread_;
  io_service io_service_;
  io_service::work work_;
  BackendConnPool* backend_conn_pool_;
};

class WorkerPool {
public:
  explicit WorkerPool(size_t concurrency)
      : concurrency_(concurrency)
      , workers_(new WorkerContext[concurrency])
      , next_worker_(0) { 
  }

  void StartDispatching();
  void StopDispatching();

  ClientConnection* NewClientConntion();
private:
  static thread_local int worker_id_;

  size_t concurrency_;
  WorkerContext* workers_; // TODO : use std::unique_ptr

  WorkerContext& NextWorker() {
    return workers_[next_worker_++ % concurrency_];
  }
  size_t next_worker_;
};

}

#endif // _WORKER_POOL_H_

