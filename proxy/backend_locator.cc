#include "backend_locator.h"

#include "base/logging.h"

#include "config.h"
#include "consist_hash.h"

namespace yarmproxy {

const char DEFAULT_GROUP[] = "DEFAULT";

// static const char * backend_nodes = "127.0.0.1:21211=2000;127.0.0.1:21212=2000;127.0.0.1:21213=2000;127.0.0.1:21214=2000;127.0.0.1:21215=2000;";
static const char * backend_nodes = "127.0.0.1:11211=2000;127.0.0.1:11212=2000;127.0.0.1:11213=2000;127.0.0.1:11214=2000;127.0.0.1:11215=2000;";
// static const char * backend_nodes = "127.0.0.1:11211=2000;127.0.0.1:11212=2000";
// static const char * backend_nodes = "127.0.0.1:11211=2000";

bool BackendLoactor::Initialize() {
  for(auto& cluster : Config::Instance().clusters()) {
  //ProtocolType protocol_;
  //std::vector<std::string> namespaces_;
  //std::vector<std::pair<std::string, int>> backends_;
    Continuum * continuum = new Continuum(cluster.backends_);
    for(auto& ns : cluster.namespaces_) {
      std::ostringstream oss;
      oss << int(cluster.protocol_) << "_" << (ns == "_default" ? "" : ns.c_str());
      clusters_continum_.emplace(oss.str(), continuum);
      LOG_DEBUG << "BackendLoactor ns=" << oss.str() << " continium=" << continuum;
    }
  }
  return true;
}

bool BackendLoactor::Initialize2() {
  {
    static const char redis_backend_nodes[] = "127.0.0.1:6379=2000;127.0.0.1:6380=2000;127.0.0.1:6381=2000";
    // static const char redis_backend_nodes[] = "127.0.0.1:6379=2000";
    // static const char redis_backend_nodes[] = "127.0.0.1:16379=2000";
    // static const char redis_backend_nodes[] = "127.0.0.1:6379=2000;127.0.0.1:11111=2000;127.0.0.1:22222=2000";

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

static const std::string& DefaultNamespace(ProtocolType protocol) {
  static std::string REDIS_NS_DEFAULT = "0_";
  static std::string MEMCACHED_NS_DEFAULT = "1_";
  switch(protocol) {
  case ProtocolType::REDIS:
    return REDIS_NS_DEFAULT;
  case ProtocolType::MEMCACHED:
    return MEMCACHED_NS_DEFAULT;
  default:
    assert(false);
    return REDIS_NS_DEFAULT;
  }
}

std::string BackendLoactor::KeyNamespace(const char * key, size_t len, ProtocolType protocol) {
  std::ostringstream oss;
  oss << int(protocol) << "_";
  const char * p = static_cast<const char *>(memchr(key, '#', len));
  if (p != nullptr) {
    oss << std::string(key, p - key);
  }
  return oss.str();
}

ip::tcp::endpoint BackendLoactor::Locate(const char * key, size_t len, ProtocolType protocol) {
  Continuum * continuum = nullptr;  // use shared_ptr
  auto it = clusters_continum_.find(KeyNamespace(key, len, protocol));
  if (it == clusters_continum_.end()) {
    // TODO : optional drop user request
    continuum = clusters_continum_[DefaultNamespace(protocol)];
  } else {
    continuum = it->second;
  }
  
  ip::tcp::endpoint ep = continuum->LocateCacheNode(key, len);
  LOG_DEBUG << "BackendLoactor::Locate key=" << std::string(key, len)
            << " continuum=" << continuum
            << " cache_node=" << ep;
  return ep;
}

/*
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
*/
}

