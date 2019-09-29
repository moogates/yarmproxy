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

  bool daemonize() const {
    return daemonize_;
  }
  size_t buffer_size() const {
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

  const std::string& config_file() const {
    return config_file_;
  }
  void set_config_file(const char* fname) {
    config_file_ = fname;
  }
  const std::string& listen() const {
    return listen_;
  }
  const std::string& log_file() const {
    return log_file_;
  }
  const std::string& log_level() const {
    return log_level_;
  }

  bool Reload();
private:
  std::string config_file_ = "./yarmproxy.conf";

  std::string listen_ = "127.0.0.1:11311";
  bool daemonize_ = false;

  std::string log_file_ = "./yarmproxy.log";
  std::string log_level_ = "WARN";

  size_t buffer_size_ = 4096;
public:
  std::vector<Cluster> clusters_;
private:
  Config() {}
  void set_buffer_size(size_t sz) {
    // TODO : must be 2^n
    if (sz >= 1024 && sz <= 1024 * 1024 &&
        ((sz & (sz - 1)) == 0)) {
      buffer_size_ = sz;
    }
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

