#include "proxy_server.h"
#include "logging.h"

// TODO : config argv
// TODO : timeout handling
// TODO : max backend conn resource limit
// TODO : refining hash & katama
// TODO : gracefully shutdown
// TODO : kill -HUP reload
// TODO : anti spam-request

int Daemonize();
int MaximizeFdLimit();

int main() {
  base::InitLogging("proxy.log", "TRACE");
  // loguru::g_stderr_verbosity = 0;
  // loguru::g_stderr_verbosity = -8;
  loguru::g_stderr_verbosity = 8;
  // loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;

  // Daemonize();
  MaximizeFdLimit();

  std::string endpoint("127.0.0.1:11311");
  LOG_INFO << "Service listening on " << endpoint;
  yarmproxy::ProxyServer server(endpoint); // TODO : concurrency
  server.Run();
  return 0;
}

