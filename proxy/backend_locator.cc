#include "backend_locator.h"

#include "base/logging.h"

#include "config.h"
#include "consist_hash.h"

namespace yarmproxy {

bool BackendLoactor::Initialize() {
  for(auto& cluster : Config::Instance().clusters()) {
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

extern size_t kMaxNamespaceLength;
static std::string KeyNamespace(const char * key, size_t len, ProtocolType protocol) {
  std::ostringstream oss;
  oss << int(protocol) << "_";
  const char * p = static_cast<const char *>(memchr(key, '#', std::min(len, kMaxNamespaceLength + 1)));
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

}

