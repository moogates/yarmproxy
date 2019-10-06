#ifndef _YAMPROXY_CONFIG_H_
#define _YAMPROXY_CONFIG_H_

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace yarmproxy {

enum class ProtocolType;

class Config {
public:
  static Config& Instance() {
    static Config config;
    return config;
  }

  bool Initialize();
  bool ReloadCulsters();

  bool daemonize() const {
    return daemonize_;
  }
  int worker_threads() const {
    return worker_threads_;
  }

  int max_namespace_length() const {
    return max_namespace_length_;
  }
  size_t buffer_size() const {
    return buffer_size_;
  }
  struct Backend {
    Backend(std::string&& host, int port, size_t weight)
      : host_(host)
      , port_(port)
      , weight_(weight) {
    }
    std::string host_;
    int port_;
    size_t weight_;
  };
  struct Cluster {
    ProtocolType protocol_;
    std::vector<std::string> namespaces_;
    std::vector<Backend>     backends_;
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

  bool worker_cpu_affinity() const {
    return worker_cpu_affinity_;
  }

  const std::vector<Cluster>& clusters() const {
    return clusters_;
  }
private:
  std::string config_file_ = "./yarmproxy.conf";
  using TokensHandler = std::function<bool(const std::vector<std::string>& tokens)>;
  bool TraverseConfFile(TokensHandler handler);

  std::string listen_ = "127.0.0.1:11311";
  bool daemonize_ = false;
  int worker_threads_ = 0;
  int max_namespace_length_ = 4;

  int worker_max_idle_backends_    = 32;
  size_t worker_buffer_size_       = 4096;
  size_t worker_buffer_trunk_size_ = 0;
  bool worker_cpu_affinity_ = false;

  std::string log_file_ = "./yarmproxy.log";
  std::string log_level_ = "WARN";

  size_t buffer_size_ = 4096;
  std::vector<Cluster> clusters_;
private:
  Config() {}

  // TODO : add them in parser class
  bool ApplyTokens(const std::vector<std::string>& tokens);
  bool ApplyGlobalTokens(const std::vector<std::string>& tokens);
  bool ApplyClusterTokens(const std::vector<std::string>& tokens);
  bool ApplyWorkerTokens(const std::vector<std::string>& tokens);
  bool ApplyLogTokens(const std::vector<std::string>& tokens);

  void PushSubcontext(const std::string& subcontext);
  void PopSubcontext();
  std::string context_;
  std::string error_msg_;
};

}

#endif // _YAMPROXY_CONFIG_H_

