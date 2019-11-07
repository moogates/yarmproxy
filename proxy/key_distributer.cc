#include "key_distributer.h"

#include "logging.h"

#include "doobs_hash.h"

namespace yarmproxy {

KeyDistributer::KeyDistributer(const std::vector<Config::Backend>& backends) {
  for(auto& backend : backends) {
    LOG_DEBUG << "KeyDistributerctor host=" << backend.host_
              << " port=" << backend.port_
              << " weight=" << backend.weight_;
    auto ep = Endpoint(boost::asio::ip::address_v4::from_string(backend.host_),
                                backend.port_);
    weighted_nodes_.emplace(ep, backend.weight_);
  }
  BuildCachePoints();
}


bool KeyDistributer::BuildCachePoints() {
  if (weighted_nodes_.empty()) {
    LOG_WARN << "KeyDistributer::BuildCachePoints empty node list!";
    return false;
  }

  std::vector<CachePoint> new_cache_points;
  for(const auto& it : weighted_nodes_) {
    char ss[64];
    for(size_t k = 0; k < it.second; ++k) {
      snprintf(ss, 63, "%lu-%lu-%u", k, it.first.address().to_v4().to_ulong(),
                                    it.first.port());
      // TODO : support various hash functions
      uint32_t hash_point = doobs_hash(ss, strlen(ss));
      new_cache_points.push_back(CachePoint(hash_point, it.first));
    }
  }

  std::sort(new_cache_points.begin(), new_cache_points.end());

  cache_points_.swap(new_cache_points);
  return true;
}

Endpoint KeyDistributer::LocateCacheNode(const char * key,
                                           size_t len) const {
  uint32_t hash = doobs_hash(key, len);
  Endpoint ep;

  auto it = std::lower_bound(cache_points_.begin(),
                             cache_points_.end(),
                             CachePoint(hash, ep));
  if (it != cache_points_.end()) {
    ep = it->endpoint;
  } else {
    if(!cache_points_.empty()) {
      ep = cache_points_.front().endpoint;
    }
  }
  return ep;
}

void KeyDistributer::Dump() {
  for(auto& entry : cache_points_) {
    LOG_DEBUG << "cache point dump - " << entry.endpoint
              << " : " << entry.hash_point;
  }
}

}

