#include "worker_pool.h"

#include <iostream>

#include "base/logging.h"

#include "allocator.h"
#include "backend_pool.h"

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

void WorkerPool::StartDispatching() {
  for(size_t i = 0; i < concurrency_; ++i) {
    WorkerContext& woker = workers_[i];
    std::atomic_bool& stopped(stopped_);
    std::thread th([&woker, &stopped, i]() {
        while(!stopped) {
        //try {
            woker.io_service_.run();
        //} catch (std::exception& e) {
        //  LOG_ERROR << "WorkerThread " << i << " io_service.run error:" << e.what();
        //}
        }
        LOG_WARN << "WorkerThread " << i << " stopped.";
      });
    woker.thread_ = std::move(th); // what to to with this thread handle?
  }
}

void WorkerPool::StopDispatching() {
  stopped_ = true;
  for(size_t i = 0; i < concurrency_; ++i) {
    workers_[i].io_service_.stop();
  }
  for(size_t i = 0; i < concurrency_; ++i) {
    LOG_WARN << "WorkerPool StopDispatching join " << i;
    workers_[i].thread_.join();
  }
}

}

