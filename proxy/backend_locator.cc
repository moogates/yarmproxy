#include "backend_locator.h"

#include "base/logging.h"

#include "consist_hash.h"

using namespace boost::asio;

namespace mcproxy {

static const char * memcached_nodes = "127.0.0.1:11211=2000;127.0.0.1:11212=2000;127.0.0.1:11213=2000;127.0.0.1:11214=2000;127.0.0.1:11215=2000;";
// static const char * memcached_nodes = "127.0.0.1:11211=2000";

//static const char * memcached_nodes = "10.3.22.42:11211=6800;"
//                              "10.3.22.43:11211=6800;"
//                              "10.3.22.119:11211=6800;"
//                              "10.3.22.120:11211=6800;"
//                              "10.3.22.121:11211=6800;"
//                              "10.3.22.122:11211=6800;"
//                              "10.3.22.123:11211=6800;"
//                              "10.3.22.124:11211=6800;"
//                              "10.3.22.125:11211=6800;"
//                              "10.3.22.126:11211=6800";


bool BackendLoactor::Initialize() {
  {
    std::string ns = "FEED";
    //std::string FEED_nodes = "10.3.17.128:11211 2800;10.3.16.210:11211 2800;10.3.16.211:11211 2800;10.3.17.149:11211 1500;10.3.20.44:11211 2700"
    //            ";10.3.20.45:11211 2800;10.3.20.46:11211 2800;10.3.20.47:11211 2800;10.3.20.48:11211 2800";

    Continuum * continuum = new Continuum;
    // if (continuum->SetCacheNodes(FEED_nodes)) {
    if (continuum->SetCacheNodes(memcached_nodes)) {
      clusters_continum_.insert(std::make_pair("FEED", continuum));
    } else {
      LOG_WARN << "加载 Continuum 失败 " << ns << "-" << memcached_nodes;
      delete continuum;
    }
  }

  {
    std::string ns = "ADUP";
    //std::string ADUP_nodes = "10.3.17.128:11211 2800;10.3.16.210:11211 2800;10.3.16.211:11211 2800;10.3.17.149:11211 1500;10.3.20.44:11211 2700"
    //                  ";10.3.20.45:11211 2800;10.3.20.46:11211 2800;10.3.20.47:11211 2800;10.3.20.48:11211 2800";
    Continuum * continuum = new Continuum;
    if (continuum->SetCacheNodes(memcached_nodes)) {
      clusters_continum_.insert(make_pair(ns, continuum));
    } else {
      LOG_WARN << "加载 Continuum 失败 : " << ns << "-" << memcached_nodes;
      delete continuum;
    }
  }

  {
    std::string ns = "DEFAULT";
    Continuum * continuum = new Continuum;
    if (continuum->SetCacheNodes(memcached_nodes)) {
      clusters_continum_.insert(make_pair(ns, continuum));
    } else {
      LOG_WARN << "加载 Continuum 失败 : " << ns << "-" << memcached_nodes;
      delete continuum;
    }
  }

  return true;
}

ip::tcp::endpoint BackendLoactor::GetEndpointByKey(const std::string& key) {
  return GetEndpointByKey(key.c_str(), key.size());
}

ip::tcp::endpoint BackendLoactor::GetEndpointByKey(const char * key, size_t len) {
  size_t delim_pos = 0;
  for (; delim_pos < len; ++ delim_pos) {
    if (key[delim_pos] == '#') {
      break;
    }
  }

  Continuum * continuum = nullptr;

  if (delim_pos != len) {
    std::map<std::string, Continuum *>::const_iterator it = clusters_continum_.find(std::string(key, delim_pos));
    if (it != clusters_continum_.end()) {
      continuum = it->second;
    }
  } else {
    delim_pos = 0;
  }

  if (continuum == nullptr) {
    continuum = clusters_continum_["DEFAULT"];
  }
  
  ip::tcp::endpoint ep = continuum->LocateCacheNode(key, len);
  LOG_DEBUG << "BackendLoactor::GetEndpointByKey key=" << std::string(key, len) << " cache_node=" << ep;

  return ep;
}

}

