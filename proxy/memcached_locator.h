#ifndef _MEMCACHED_LOCATOR_H_
#define _MEMCACHED_LOCATOR_H_

#include <string>
#include <boost/asio.hpp>

namespace mcproxy {

class Continuum;

class MemcachedLocator {
private:
  MemcachedLocator() {}
public:
  static MemcachedLocator& Instance() {
    static MemcachedLocator locator;
    return locator;
  }
  // bool InitCacheClusters();
  bool Initialize();
  boost::asio::ip::tcp::endpoint GetEndpointByKey(const char * key, size_t len);
  boost::asio::ip::tcp::endpoint GetEndpointByKey(const std::string& key);
private:
  std::map<std::string, Continuum *> clusters_continum_;
};

}

#endif // _MEMCACHED_LOCATOR_H_
