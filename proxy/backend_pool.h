#ifndef _YARMPROXY_BACKEND_POOL_H_
#define _YARMPROXY_BACKEND_POOL_H_

#include <map>
#include <memory>
#include <queue>

#include <boost/asio/ip/tcp.hpp>

namespace yarmproxy {

using Endpoint = boost::asio::ip::tcp::endpoint;

class BackendConn;
class WorkerContext;

class BackendConnPool {
public:
  BackendConnPool(WorkerContext& context) : context_(context) {}

  std::shared_ptr<BackendConn> Allocate(const Endpoint & ep);
  void Release(std::shared_ptr<BackendConn> conn);

private:
  WorkerContext& context_;
  std::map<Endpoint, std::queue<std::shared_ptr<BackendConn>>> conn_map_;  // rename to idle_conns_
  std::map<std::shared_ptr<BackendConn>, Endpoint> active_conns_;
};

}

#endif // _YARMPROXY_BACKEND_POOL_H_

