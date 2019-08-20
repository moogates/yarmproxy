#include "worker_pool.h"

#include "client_conn.h"
#include "backend_conn.h"

namespace mcproxy {

WorkerPool::Worker::Worker() : work_(io_service_), upconn_pool_(new BackendConnPool(io_service_)) {
}

void WorkerPool::StartDispatching() {
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
    woker.thread_ = std::move(th); // what to to with this thread handle?
  }
}

void WorkerPool::StopDispatching() {
  for(size_t i = 0; i < concurrency_; ++i) {
    workers_[i].io_service_.stop();
  }
}

ClientConnection* WorkerPool::NewClientConntion() {
  Worker& worker = NextWorker();
  return new ClientConnection(worker.io_service_, worker.upconn_pool_);
}

}

