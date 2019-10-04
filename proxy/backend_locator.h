#ifndef _YARMPROXY_BACKEND_LOCATOR_H_
#define _YARMPROXY_BACKEND_LOCATOR_H_

#include <string>
#include <boost/asio/ip/tcp.hpp>

namespace yarmproxy {

using Endpoint = boost::asio::ip::tcp::endpoint;

class Continuum;
enum class ProtocolType {
  REDIS     = 0,
  MEMCACHED = 1,
};

class BackendLoactor {
private:
  BackendLoactor() {}
public:
  static BackendLoactor& Instance() {
    static BackendLoactor locator;
    return locator;
  }
  bool Initialize();

  Endpoint Locate(const char * key, size_t len, ProtocolType protocol);
private:
  std::map<std::string, Continuum *> clusters_continum_;
};

}

#endif // _YARMPROXY_BACKEND_LOCATOR_H_
