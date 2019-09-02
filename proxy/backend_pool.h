#ifndef _YARMPROXY_BACKEND_POOL_H_
#define _YARMPROXY_BACKEND_POOL_H_

#include <map>
#include <memory>
#include <queue>

#include <boost/asio.hpp>
using namespace boost::asio; // TODO : minimize endpoint dependency

namespace yarmproxy {

class BackendConn;
class WorkerContext;

class BackendConnPool {
public:
  BackendConnPool(WorkerContext& context) : context_(context) {}

  std::shared_ptr<BackendConn> Allocate(const ip::tcp::endpoint & ep);
  void Release(std::shared_ptr<BackendConn> conn);

private:
  WorkerContext& context_;
  std::map<ip::tcp::endpoint, std::queue<std::shared_ptr<BackendConn>>> conn_map_;  // rename to idle_conns_
  std::map<std::shared_ptr<BackendConn>, ip::tcp::endpoint> active_conns_;
};

}

#endif // _YARMPROXY_BACKEND_POOL_H_

