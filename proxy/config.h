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

  bool Initialize(const char* conf_file);
  bool ReloadCulsters();

  bool daemonize() const {
    return daemonize_;
  }
  int backlog() const {
    return backlog_;
  }
  int worker_threads() const {
    return worker_threads_;
  }

  int max_namespace_length() const {
    return max_namespace_length_;
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
  const std::string& pid_file() const {
    return pid_file_;
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
  size_t worker_max_idle_backends() const {
    return worker_max_idle_backends_;
  }

  int client_idle_timeout() const {
    return client_idle_timeout_;
  }
  int socket_rw_timeout() const {
    return socket_rw_timeout_;
  }
  size_t buffer_size() const {
    return buffer_size_;
  }
  size_t reserved_buffer_space() const {
    return reserved_buffer_space_;
  }

  const std::vector<Cluster>& clusters() const {
    return clusters_;
  }
private:
  std::string config_file_;
  std::string pid_file_ = "/tmp/yarmproxy.pid";

  using TokensHandler = std::function<bool(const std::vector<std::string>& tokens)>;
  bool TraverseConfFile(TokensHandler handler);

  std::string listen_ = "127.0.0.1:11311";
  bool daemonize_ = false;
  int backlog_ = 1024;
  int worker_threads_ = 0;
  int max_namespace_length_ = 4;

  std::string log_file_ = "./yarmproxy.log";
  std::string log_level_ = "WARN";

  int client_idle_timeout_ = 60000; // 60,000ms(one minute)
  int socket_rw_timeout_   = 500;   // 500 ms

  // per worker config
  size_t worker_max_idle_backends_  = 64;
  size_t buffer_size_            = 4096;
  size_t reserved_buffer_space_  = 0;
  bool worker_cpu_affinity_      = false;

  std::vector<Cluster> clusters_;
private:
  Config() {}

  bool ApplyTokens(const std::vector<std::string>& tokens);
  bool ApplyGlobalTokens(const std::vector<std::string>& tokens);
  bool ApplyClusterTokens(const std::vector<std::string>& tokens);
  bool ApplyWorkerTokens(const std::vector<std::string>& tokens);

  void PushSubcontext(const std::string& subcontext);
  void PopSubcontext();

  std::string context_;
  std::string error_msg_;
};

}

#endif // _YAMPROXY_CONFIG_H_

