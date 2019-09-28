#include "proxy_server.h"

#include "base/logging.h"

// TODO : argv / config file
// TODO : idle client-conn timeout & heartbeat ?
// TODO : max backend conn resource limit
// TODO : refining hash & katama
// TODO : gracefully shutdown
// TODO : kill -HUP reload
// TODO : anti spam-request
// TODO : better detail hiding
// TODO : limit max data size
// TODO : redis mset error handling
// TODO : benchmarking, vs. twemproxy
// TODO : c++11 style member var init
// TODO : cpu affinity
// TODO : memcached key_len_limit=250, body_size_limit=1M
// TODO : 9.27 跑了一晚上，redis返回错误提示：-ERR max number of clients reached
// TODO : 超时处理, backend_conn / client_conn 的读写，都要处理超时

int Daemonize();
int MaximizeFdLimit();

int main() {
  // base::InitLogging("proxy.log", "DEBUG");
  // base::InitLogging("proxy.log", "INFO");
  // base::InitLogging("proxy.log", "WARN");
  loguru::g_stderr_verbosity = -8;
  // loguru::g_stderr_verbosity = 8;
  // loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
  // loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
  // loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;

  // Daemonize();
  MaximizeFdLimit();

  std::string endpoint("127.0.0.1:11311");
  LOG_INFO << "Service listening on " << endpoint;
  yarmproxy::ProxyServer server(endpoint); // TODO : concurrency
  server.Run();
  return 0;
}

