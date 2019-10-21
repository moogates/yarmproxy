#include "backend_locator.h"

#include "logging.h"

#include "config.h"
#include "backend_continuum.h"
#include "protocol_type.h"

namespace yarmproxy {

static const char * ProtocolNs(ProtocolType protocol) {
  switch(protocol) {
  case ProtocolType::MEMCACHED:
    return "m";
  case ProtocolType::REDIS:
    return "r";
  default:
    return "";
  }
}

bool BackendLocator::Initialize() {
  if (Config::Instance().clusters().empty()) {
    return false;
  }
  for(auto& cluster : Config::Instance().clusters()) {
    std::shared_ptr<BackendContinuum> continuum(
        new BackendContinuum(cluster.backends_));
    for(auto& ns : cluster.namespaces_) {
      std::ostringstream oss;
      oss << ProtocolNs(cluster.protocol_) << "/" << (ns == "_" ? "" : ns.c_str());
      namespace_continum_.emplace(oss.str(), continuum);
      LOG_DEBUG << "BackendLocator ns=" << oss.str()
                << " continium=" << continuum;
    }
  }
  return true;
}

static const std::string& DefaultNamespace(ProtocolType protocol) {
  static std::string REDIS_NS_DEFAULT = "0/";
  static std::string MEMCACHED_NS_DEFAULT = "1/";
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

static std::string KeyNamespace(const char * key, size_t len, ProtocolType protocol) {
  std::ostringstream oss;
  oss << ProtocolNs(protocol) << "/";
  const char * p = static_cast<const char *>(memchr(key, ':',
        std::min(int(len), Config::Instance().max_namespace_length() + 1)));
  if (p != nullptr) {
    oss << std::string(key, p - key);
  }
  return oss.str();
}

Endpoint BackendLocator::Locate(const char * key, size_t len, ProtocolType protocol) {
  std::shared_ptr<BackendContinuum> continuum;
  auto it = namespace_continum_.find(KeyNamespace(key, len, protocol));
  if (it == namespace_continum_.end()) {
    // TODO : optional drop user request
    continuum = namespace_continum_[DefaultNamespace(protocol)];
  } else {
    continuum = it->second;
  }

  Endpoint ep = continuum->LocateCacheNode(key, len);
  return ep;
}

}

