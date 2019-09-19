#include "backend_locator.h"

#include "base/logging.h"

#include "consist_hash.h"

using namespace boost::asio;

namespace yarmproxy {
const char DEFAULT_GROUP[] = "DEFAULT";

// static const char * backend_nodes = "127.0.0.1:21211=2000;127.0.0.1:21212=2000;127.0.0.1:21213=2000;127.0.0.1:21214=2000;127.0.0.1:21215=2000;";
static const char * backend_nodes = "127.0.0.1:11211=2000;127.0.0.1:11212=2000;127.0.0.1:11213=2000;127.0.0.1:11214=2000;127.0.0.1:11215=2000;";
// static const char * backend_nodes = "127.0.0.1:11211=2000;127.0.0.1:11212=2000";
// static const char * backend_nodes = "127.0.0.1:11211=2000";

//static const char * backend_nodes = "10.3.22.42:11211=6800;"
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
    // static const char redis_backend_nodes[] = "127.0.0.1:6379=2000;127.0.0.1:6380=2000;127.0.0.1:6381=2000";
    // static const char redis_backend_nodes[] = "127.0.0.1:6379=2000";
    // static const char redis_backend_nodes[] = "127.0.0.1:16379=2000";
    static const char redis_backend_nodes[] = "127.0.0.1:6379=2000;127.0.0.1:11111=2000;127.0.0.1:22222=2000";

    char group[] = "REDIS_bj";
    Continuum * continuum = new Continuum;
    // if (continuum->SetCacheNodes(FEED_nodes)) {
    if (continuum->SetCacheNodes(redis_backend_nodes)) {
      clusters_continum_.insert(std::make_pair(group, continuum));
    } else {
      LOG_WARN << "加载 Continuum 失败 gruop=" << group << "-" << redis_backend_nodes;
      delete continuum;
    }
  }

  {
    char group[] = "MEMCACHED_bj";
    Continuum * continuum = new Continuum;
    if (continuum->SetCacheNodes(backend_nodes)) {
      clusters_continum_.insert(std::make_pair(group, continuum));
    } else {
      LOG_WARN << "加载 Continuum 失败, group=" << group << "-" << backend_nodes;
      delete continuum;
    }
  }

  {
    Continuum * continuum = new Continuum;
    if (continuum->SetCacheNodes(backend_nodes)) {
      clusters_continum_.insert(std::make_pair(DEFAULT_GROUP, continuum));
    } else {
      LOG_WARN << "加载 Continuum 失败 : group=" << DEFAULT_GROUP << "-" << backend_nodes;
      delete continuum;
    }
  }

  return true;
}

ip::tcp::endpoint BackendLoactor::Locate(const std::string& key, const char* group) {
  return Locate(key.c_str(), key.size(), group);
}

ip::tcp::endpoint BackendLoactor::Locate(const char * key, size_t len, const char* group) {
  Continuum * continuum = nullptr;

  auto it = clusters_continum_.find(group);
  if (it == clusters_continum_.end()) {
    continuum = clusters_continum_[DEFAULT_GROUP];
  } else {
    continuum = it->second;
  }
  
  ip::tcp::endpoint ep = continuum->LocateCacheNode(key, len);
  // LOG_DEBUG << "BackendLoactor::Locate key=" << std::string(key, len) << " cache_node=" << ep;

  return ep;
}

}

