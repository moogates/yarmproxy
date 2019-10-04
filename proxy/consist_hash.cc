#include "consist_hash.h"

#include "base/logging.h"

#include "doobs_hash.h"

namespace yarmproxy {

Continuum::Continuum(const std::vector<Config::Backend>& backends) {
  for(auto& backend : backends) {
    LOG_DEBUG << "Continuum ctor host=" << backend.host_
              << " port=" << backend.port_
              << " weight=" << backend.weight_;
    auto ep = Endpoint(boost::asio::ip::address_v4::from_string(backend.host_),
                                backend.port_);
    cache_nodes_.emplace(ep, backend.weight_);
  }
  RebuildCachePoints();
}


bool Continuum::RebuildCachePoints() {
  if (cache_nodes_.empty()) {
    LOG_WARN << "Continuum::RebuildCachePoints empty node list!";
    return false;
  }

  std::vector<CachePoint> new_cache_points;
  // CacheNodeMap::const_iterator it = cache_nodes_.begin();
  for(const auto& it : cache_nodes_)
  {
    char ss[64];
    for(size_t k = 0; k < it.second; ++k)
    {
      snprintf(ss, 63, "%u-%lu-%u", k, it.first.address().to_v4().to_ulong(), it.first.port());
      uint32_t hash_point = doobs_hash(ss, strlen(ss));  // TODO : use murmur hash
      new_cache_points.push_back(CachePoint(hash_point, it.first));
    }
  }

  std::sort(new_cache_points.begin(), new_cache_points.end());

  {
    boost::unique_lock<boost::shared_mutex> wlock(cache_points_mutex_);
    cache_points_.swap(new_cache_points);
  }
  return true;
}

Endpoint Continuum::LocateCacheNode(const char * key, size_t len) const
{
  uint32_t hash = doobs_hash(key, len);
  Endpoint ep;

  // boost::shared_lock<boost::shared_mutex> rlock(cache_points_mutex_); // TODO : lock free
  auto it = std::lower_bound(cache_points_.begin(), cache_points_.end(), CachePoint(hash, ep));
  if (it != cache_points_.end()) {
    ep = it->endpoint;
  } else {
    if(!cache_points_.empty()) {
      ep = cache_points_.front().endpoint;
    }
  }
  return ep;
}

void Continuum::Dump() {
  boost::shared_lock<boost::shared_mutex> rlock(cache_points_mutex_);
  for(auto& entry : cache_points_) {
    LOG_DEBUG << "cache point dump - " << entry.endpoint << " : " << entry.hash_point;
  }
}

}

