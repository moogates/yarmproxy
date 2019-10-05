#ifndef _YARMPROXY_CONSIST_HASH_H_
#define _YARMPROXY_CONSIST_HASH_H_

#include <string>
#include <map>
#include <vector>
#include <stdint.h>

#include <boost/asio/ip/tcp.hpp>

#include "config.h"

namespace yarmproxy {
using Endpoint = boost::asio::ip::tcp::endpoint;

class BackendContinuum {
public:
  BackendContinuum(const std::vector<Config::Backend>& backends);
  Endpoint LocateCacheNode(const char * key, size_t len) const;
  void Dump();

private:
  bool BuildCachePoints();

  // 每个 cache node 会对应 continuum 上数百个(按比例)cache point
  struct CachePoint {
    CachePoint(uint32_t hp, const Endpoint & ep)
      : hash_point(hp)
      , endpoint(ep)
    {}

    uint32_t hash_point;  // point on continuum circle
    Endpoint endpoint;

    bool operator<(const CachePoint& r) const { return hash_point < r.hash_point; }
  };  

  std::map<Endpoint, size_t> weighted_nodes_; //服务器信息
  std::vector<CachePoint> cache_points_;
};

}

#endif // _YARMPROXY_CONSIST_HASH_H_

