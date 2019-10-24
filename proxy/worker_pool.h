#ifndef _YARMPROXY_WORKER_POOL_H_
#define _YARMPROXY_WORKER_POOL_H_

#include <thread>
#include <atomic>
#include <boost/asio.hpp>

namespace yarmproxy {

class BackendConnPool;
class KeyLocator;
class Allocator;

class WorkerContext {
public:
  WorkerContext();
  std::thread thread_;
  boost::asio::io_service io_service_;
  boost::asio::io_service::work work_;
  std::shared_ptr<KeyLocator> key_locator_;
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
      , stopped_(false) {
  }

  void StartDispatching();
  void StopDispatching();
  void OnLocatorUpdated(std::shared_ptr<KeyLocator> locator);

  WorkerContext& NextWorker() {
    return workers_[next_worker_++ % concurrency_];
  }
private:
  size_t concurrency_;
  WorkerContext* workers_; // TODO : use std::unique_ptr

  std::atomic_bool stopped_;
  size_t next_worker_ = 0;
};

}

#endif // _YARMPROXY_WORKER_POOL_H_

