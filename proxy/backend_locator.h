#ifndef _YARMPROXY_BACKEND_LOCATOR_H_
#define _YARMPROXY_BACKEND_LOCATOR_H_

#include <string>
#include <boost/asio.hpp>

namespace yarmproxy {

class Continuum;

extern const char DEFAULT_GROUP[];

class BackendLoactor {
private:
  BackendLoactor() {}
public:
  static BackendLoactor& Instance() {
    static BackendLoactor locator;
    return locator;
  }
  bool Initialize();
  boost::asio::ip::tcp::endpoint GetEndpointByKey(const char * key, size_t len, const char* group = DEFAULT_GROUP);
  boost::asio::ip::tcp::endpoint GetEndpointByKey(const std::string& key, const char* group = DEFAULT_GROUP);
private:
  std::map<std::string, Continuum *> clusters_continum_;
};

}

#endif // _YARMPROXY_BACKEND_LOCATOR_H_
