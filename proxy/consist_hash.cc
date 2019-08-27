#include "consist_hash.h"

#include "doobs_hash.h"
#include "base/logging.h"

namespace mcproxy {

bool Continuum::RebuildCachePoints() {
  if(cache_nodes_.empty()) {
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

ip::tcp::endpoint Continuum::LocateCacheNode(const char * key, size_t len) const
{
  uint32_t hash = doobs_hash(key, len);
  ip::tcp::endpoint ep;

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

// TODO : test ParseNodesConfig
bool Continuum::ParseNodesConfig(const std::string & config, CacheNodeMap * parsed) const {
  {
    size_t pos = 0, prev_pos = 0;
    while(pos < config.size()) {
      pos = config.find(':', prev_pos);
      if (pos == std::string::npos) {
        LOG_WARN << "ParseNodesConfig host error, config=" << config;
        return false;
      }
      std::string host = config.substr(prev_pos, pos - prev_pos);
      LOG_DEBUG << "ParseNodesConfig host=" << host << " ok, config=" << config;

      prev_pos = pos + 1;
      pos = config.find('=', prev_pos);
      if (pos == std::string::npos) {
        LOG_WARN << "ParseNodesConfig port error, config=" << config;
        return false;
      }
      int port = std::stoi(config.substr(prev_pos, pos - prev_pos));

      prev_pos = pos + 1;
      pos = config.find(';', prev_pos);
      int weight = 0;
      if (pos == std::string::npos) {
        weight = std::stoi(config.substr(prev_pos));
      } else {
        weight = std::stoi(config.substr(prev_pos, pos - prev_pos));
        ++pos;
        prev_pos = pos;
      }
      LOG_DEBUG << "ParseNodesConfig " << host << ":" << port << " weight=" << weight << config;
      parsed->insert(std::make_pair(ip::tcp::endpoint(ip::address_v4::from_string(host), port), weight));
    }
    return true;
  }

//std::vector<std::string> splited;
//boost::split(splited, config, boost::is_any_of(",;: "));
//if(splited.back().empty()) {
//  splited.pop_back();
//}

//if(splited.empty() || splited.size() % 3 != 0)
//{
//  LOG_WARN << "incorrect cache nodes config: " << config;
//  return false;
//}

//parsed->clear();

//int port, size;
//for(size_t i = 0; i < splited.size(); i += 3) {
//  try {
//    port = std::stoi(splited[i + 1]);
//    size = std::stoi(splited[i + 2]);
//  } catch(...) {
//    LOG_WARN << "incorrect cache nodes config: " << config;
//    return false;
//  }

//  if(port <= 0 || size <= 0)
//  {
//    LOG_WARN << "incorrect cache nodes config: " << config;
//    return false;
//  }
//  parsed->insert(std::make_pair(ip::tcp::endpoint(ip::address_v4::from_string(splited[i]), port), size));
//}
//return true;
}

bool Continuum::SetCacheNodes(const std::string & cache_nodes) {
  CacheNodeMap cache_node_map;
  if(!ParseNodesConfig(cache_nodes, &cache_node_map)) {
    return false;
  }
  cache_nodes_.swap(cache_node_map);
  return RebuildCachePoints();
}

void Continuum::Dump() {
  boost::shared_lock<boost::shared_mutex> rlock(cache_points_mutex_);
  for(auto& entry : cache_points_) {
    LOG_DEBUG << "cache point dump - " << entry.endpoint << " : " << entry.hash_point;
  }
}

}

