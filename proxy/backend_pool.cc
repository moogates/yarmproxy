#include "backend_pool.h"

#include "backend_conn.h"
#include "logging.h"

namespace yarmproxy {

std::shared_ptr<BackendConn> BackendConnPool::Allocate(const ip::tcp::endpoint & ep){
  {
  //std::shared_ptr<BackendConn> backend(new BackendConn(context_, ep));
  //return backend;
  }
  std::shared_ptr<BackendConn> backend;
  auto it = conn_map_.find(ep);
  if ((it != conn_map_.end()) && (!it->second.empty())) {
    backend = it->second.front();
    it->second.pop();
    LOG_DEBUG << "BackendConnPool::Allocate reuse, backend=" << backend.get() << " ep=" << ep << ", idles=" << it->second.size();
  } else {
    backend.reset(new BackendConn(context_, ep));
    LOG_DEBUG << "BackendConnPool::Allocate create, backend=" << backend.get() << " ep=" << ep;
  }
  auto res = active_conns_.insert(std::make_pair(backend, ep));
  assert(res.second);
  return backend;
}

void BackendConnPool::Release(std::shared_ptr<BackendConn> backend) {
  {
  //LOG_WARN << "BackendConnPool::Release delete";
  //backend->Close(); // necessary, to trigger the callbacks
  //return;
  }

  const auto ep_it = active_conns_.find(backend);
  if (ep_it == active_conns_.end()) {
    assert(false);
    backend->Close();
    return;
  }

  const ip::tcp::endpoint & ep = ep_it->second;
  LOG_DEBUG << "BackendConnPool::Release backend=" << backend.get() << " ep=" << ep;
  active_conns_.erase(ep_it);

  if (!backend->recyclable()) {
    LOG_DEBUG << "BackendConnPool::Release unrecyclable backend=" << backend.get()
             << " reply_complete=" << backend->reply_complete();
    backend->Close();
    return;
  }

  const auto it = conn_map_.find(ep);
  if (it == conn_map_.end()) {
    backend->Reset();
    // backend->buffer()->Reset();
    conn_map_[ep].push(backend);
    LOG_DEBUG << "BackendConnPool::Release ok, backend=" << backend.get() << " ep=" << ep << " pool_size=1";
  } else {
    const static size_t kMaxConnPerEndpoint = 64;
    if (it->second.size() >= kMaxConnPerEndpoint){
      LOG_WARN << "BackendConnPool::Release overflow, backend=" << backend.get() << " ep=" << ep << " destroyed, pool_size=" << it->second.size();
      backend->Close();
    } else {
      backend->Reset();
      // backend->buffer()->Reset();
      it->second.push(backend);
      LOG_DEBUG << "BackendConnPool::Release ok, backend=" << backend.get() << " ep=" << ep << " pool_size=" << it->second.size();
    }
  }
}

}

