#include "proxy_server.h"
#include "logging.h"

int main() {
  // base::InitLogging("yarmproxy.log", "TRACE");
  loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
  // loguru::g_stderr_verbosity = 0;
  // loguru::g_stderr_verbosity = -8;

  std::string endpoint("127.0.0.1:11311");
  LOG_INFO << "Service listening on " << endpoint;
  yarmproxy::ProxyServer server(endpoint); // TODO : concurrency
  server.Run();
  return 0;
}

