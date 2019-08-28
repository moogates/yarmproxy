#ifndef _YARMPROXY_WORKER_POOL_H_
#define _YARMPROXY_WORKER_POOL_H_

#include <thread>
#include <boost/asio.hpp>

namespace yarmproxy {
using namespace boost::asio;

class BackendConnPool;
class Allocator;

class WorkerContext {
public:
  WorkerContext();
  std::thread thread_;
  io_service io_service_;
  io_service::work work_;
  BackendConnPool* backend_conn_pool();
private:
  BackendConnPool* backend_conn_pool_;
public:
  Allocator* allocator_;
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

  WorkerContext& NextWorker() {
    return workers_[next_worker_++ % concurrency_];
  }
private:
  static thread_local int worker_id_;

  size_t concurrency_;
  WorkerContext* workers_; // TODO : use std::unique_ptr

  size_t next_worker_;
};

}

#endif // _YARMPROXY_WORKER_POOL_H_

