#ifndef _YAMPROXY_CONFIG_H_
#define _YAMPROXY_CONFIG_H_

#include <cstddef>
#include <string>
#include <vector>

namespace yarmproxy {

class Config {
public:
  static Config& Instance() {
    static Config config;
    return config;
  }

  void set_daemonize(bool b) {
    daemonize_ = b;
  }
  bool daemonize() const {
    return daemonize_;
  }
  size_t buffer_size() const {
    return buffer_size_;
  }

  void set_buffer_size(size_t sz) {
    // TODO : must be 2^n
    if (sz >= 1024 && sz <= 1024 * 1024 &&
        ((sz & (sz - 1)) == 0)) {
      buffer_size_ = sz;
    }
  }
  size_t set_buffer_size() const {
    return buffer_size_;
  }
  enum class ProtocolType {
    REDIS     = 0,
    MEMCACHED = 1,
  };
  struct Cluster {
    ProtocolType protocol_;
    std::vector<std::string> namespaces_;
    std::vector<std::pair<std::string, int>> weighted_backends_;
  };

  bool Reload();
private:
  std::string config_file_ = "./yarmproxy.conf";

  bool daemonize_ = false;
  size_t buffer_size_ = 4096;

  std::vector<Cluster> clusters_;
private:
  Config() {
  }

  // TODO : add them in parser class
  bool ApplyTokens(const std::vector<std::string>& tokens);
  bool ApplyGlobalTokens(const std::vector<std::string>& tokens);
  bool ApplyClusterTokens(const std::vector<std::string>& tokens);
  bool ApplyLogTokens(const std::vector<std::string>& tokens);

  void PushSubcontext(const std::string& subcontext);
  void PopSubcontext();
  std::string context_;
  std::string error_msg_;
};

}

#endif // _YAMPROXY_CONFIG_H_

