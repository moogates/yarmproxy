#include "worker_pool.h"

#include <iostream>

#include "allocator.h"
#include "backend_pool.h"
#include "config.h"
#include "logging.h"
#include "key_locator.h"

namespace yarmproxy {

#define SLAB_SIZE (4*1024) // TODO : use g_config_.buffer_size

WorkerContext::WorkerContext()
    : work_(io_service_)
    , backend_conn_pool_(nullptr)
    , allocator_(new Allocator(Config::Instance().buffer_size(),
          Config::Instance().reserved_buffer_space())) {
}

BackendConnPool* WorkerContext::backend_conn_pool() {
  if (backend_conn_pool_ == nullptr) {
    backend_conn_pool_ = new BackendConnPool(*this);
  }
  return backend_conn_pool_;
}

void WorkerPool::OnLocatorUpdated(std::shared_ptr<KeyLocator> locator) {
  for(size_t i = 0; i < concurrency_; ++i) {
    WorkerContext& worker = workers_[i];
    worker.io_service_.post([&worker, locator]() {
          worker.key_locator_ = locator;
        });
  }
}

void WorkerPool::StartDispatching() {
  for(size_t i = 0; i < concurrency_; ++i) {
    WorkerContext& woker = workers_[i];
    std::atomic_bool& stopped(stopped_);
    std::thread th([&woker, &stopped, i]() {
        while(!stopped) {
          try {
            woker.io_service_.run();
          } catch (std::exception& e) {
            LOG_ERROR << "WorkerThread " << i
                      << " io_service.run error " << e.what();
          }
        }
        LOG_ERROR << "WorkerThread " << i << " stopped.";
      });
    woker.thread_ = std::move(th);
  }
}

void WorkerPool::StopDispatching() {
  stopped_ = true;
  for(size_t i = 0; i < concurrency_; ++i) {
    workers_[i].io_service_.stop();
  }
  for(size_t i = 0; i < concurrency_; ++i) {
    workers_[i].thread_.join();
    LOG_WARN << "StopDispatching joined WorkerThread " << i;
  }
}

}

