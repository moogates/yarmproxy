#include "proxy_server.h"
#include "base/logging.h"

int main() {
  // base::InitLogging("proxy.log", "TRACE");
  // loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
  loguru::g_stderr_verbosity = 8;

  std::string endpoint("127.0.0.1:11311");
	LOG_INFO << "Service listening on " << endpoint;
  mcproxy::ProxyServer server(endpoint);
  server.Run();
  return 0;
}

