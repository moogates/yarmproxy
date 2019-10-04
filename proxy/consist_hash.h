#ifndef _YARMPROXY_CONSIST_HASH_H_
#define _YARMPROXY_CONSIST_HASH_H_

#include <string>
#include <map>
#include <vector>
#include <stdint.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/thread/shared_mutex.hpp>

#include "config.h"

namespace yarmproxy {
using Endpoint = boost::asio::ip::tcp::endpoint;

using CacheNodeMap = std::map<Endpoint, size_t>;

class Continuum {
public:
  Continuum(const std::vector<Config::Backend>& backends);
  Endpoint LocateCacheNode(const char * key, size_t len) const;
  void Dump();

private:
  bool RebuildCachePoints();

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

  CacheNodeMap cache_nodes_; //服务器信息
  std::vector<CachePoint> cache_points_;
  // TODO : clone to each thread to avoid mutex
  mutable boost::shared_mutex cache_points_mutex_;
};

}

#endif // _YARMPROXY_CONSIST_HASH_H_

