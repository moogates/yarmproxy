#include "worker_pool.h"

#include "client_conn.h"
#include "backend_conn.h"

namespace mcproxy {

WorkerContext::WorkerContext() : work_(io_service_), backend_conn_pool_(new BackendConnPool(io_service_)) {
}

thread_local int WorkerPool::worker_id_;

void WorkerPool::StartDispatching() {
  for(size_t i = 0; i < concurrency_; ++i) {
    WorkerContext& woker = workers_[i];
    std::thread th([&woker, i]() {
          WorkerPool::worker_id_ = i;
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
  WorkerContext& worker = NextWorker();
  return new ClientConnection(worker);
}

}

