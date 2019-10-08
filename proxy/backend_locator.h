#ifndef _YARMPROXY_BACKEND_LOCATOR_H_
#define _YARMPROXY_BACKEND_LOCATOR_H_

#include <string>
#include <memory>
#include <boost/asio/ip/tcp.hpp>

namespace yarmproxy {

using Endpoint = boost::asio::ip::tcp::endpoint;

class BackendContinuum;
enum class ProtocolType {
  REDIS     = 0,
  MEMCACHED = 1,
};

class BackendLocator {
public:
  BackendLocator() {}
  bool Initialize();
  Endpoint Locate(const char * key, size_t len, ProtocolType protocol);
private:
  std::map<std::string, std::shared_ptr<BackendContinuum>> namespace_continum_;
};

}

#endif // _YARMPROXY_BACKEND_LOCATOR_H_
