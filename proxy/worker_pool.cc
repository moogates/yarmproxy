#include "worker_pool.h"

#include "logging.h"

#include "allocator.h"
#include "backend_conn.h"
// #include "client_conn.h"

namespace yarmproxy {

#define SLAB_SIZE (4*1024) // TODO : use c++11 enum

WorkerContext::WorkerContext()
    : work_(io_service_)
    , backend_conn_pool_(nullptr)
    , allocator_(new Allocator(SLAB_SIZE, 16)) {
}

BackendConnPool* WorkerContext::backend_conn_pool() {
  if (backend_conn_pool_ == nullptr) {
    backend_conn_pool_ = new BackendConnPool(*this);
  }
  return backend_conn_pool_;
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

}

