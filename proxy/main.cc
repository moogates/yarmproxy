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

int Daemonize();
int MaximizeFdLimit();

int main() {
  // base::InitLogging("proxy.log", "TRACE");
  base::InitLogging("proxy.log", "WARN");
  // loguru::g_stderr_verbosity = 0;
  // loguru::g_stderr_verbosity = -8;
  // loguru::g_stderr_verbosity = 8;
  loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;

  // Daemonize();
  MaximizeFdLimit();

  std::string endpoint("127.0.0.1:11311");
  LOG_INFO << "Service listening on " << endpoint;
  yarmproxy::ProxyServer server(endpoint); // TODO : concurrency
  server.Run();
  return 0;
}

