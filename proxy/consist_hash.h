#ifndef _CONSIST_HASH_H_
#define _CONSIST_HASH_H_

#include <string>
#include <map>
#include <vector>
#include <stdint.h>

#include <boost/asio.hpp>
#include <boost/thread/shared_mutex.hpp>

using namespace boost::asio;

namespace mcproxy {

typedef std::map<ip::tcp::endpoint, size_t> CacheNodeMap;

class Continuum
{
public:
  // 传入配置的格式 : "10.3.17.127:11211 2000;10.3.17.127:11212 3000;10.3.17.127:11213 4000"
  // 端口后面, 是内存大小, 以M为单位
  bool SetCacheNodes(const std::string & cache_nodes);

  ip::tcp::endpoint LocateCacheNode(const char * key, size_t len) const;

  void Dump();

  //std::vector<std::pair<uint32_t, uint32_t> > GetServerRanges(const std::string & endpoint);
private:
  bool ParseNodesConfig(const std::string & s, CacheNodeMap * parsed) const;
  bool RebuildCachePoints();

  // 每个 cache node 会对应 continuum 上数百个(按比例)的cache point
  struct CachePoint
  {
    CachePoint(uint32_t point, const ip::tcp::endpoint & ep)
      : hash_point(point)
      , endpoint(ep)
    {}

    uint32_t hash_point;  // point on continuum circle
    ip::tcp::endpoint endpoint;

    bool operator<(const CachePoint & r) const { return hash_point < r.hash_point; }
  };  

  CacheNodeMap cache_nodes_; //服务器信息
  std::vector<CachePoint> cache_points_;
  // TODO : clone to each thread to avoid mutes
  mutable boost::shared_mutex cache_points_mutex_;
};

}

#endif // _CONSIST_HASH_H_

