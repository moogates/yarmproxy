#ifndef _BACKEND_LOCATOR_H_
#define _BACKEND_LOCATOR_H_

#include <string>
#include <boost/asio.hpp>

namespace mcproxy {

class Continuum;

class BackendLoactor {
private:
  BackendLoactor() {}
public:
  static BackendLoactor& Instance() {
    static BackendLoactor locator;
    return locator;
  }
  bool Initialize();
  boost::asio::ip::tcp::endpoint GetEndpointByKey(const char * key, size_t len);
  boost::asio::ip::tcp::endpoint GetEndpointByKey(const std::string& key);
private:
  std::map<std::string, Continuum *> clusters_continum_;
};

}

#endif // _BACKEND_LOCATOR_H_
