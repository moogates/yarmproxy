#ifndef _YARMPROXY_KEY_LOCATOR_H_
#define _YARMPROXY_KEY_LOCATOR_H_

#include <string>
#include <memory>
#include <boost/asio/ip/tcp.hpp>

namespace yarmproxy {

using Endpoint = boost::asio::ip::tcp::endpoint;

class KeyDistributer;
enum class ProtocolType;

class KeyLocator {
public:
  KeyLocator() {}
  bool Initialize();
  Endpoint Locate(const char * key, size_t len, ProtocolType protocol);
private:
  std::map<std::string, std::shared_ptr<KeyDistributer>> namespace_continum_;
};

}

#endif // _YARMPROXY_KEY_LOCATOR_H_
