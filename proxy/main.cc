#include "proxy_server.h"
#include "logging.h"

void MaximizeFdLimit() {
#ifndef _WINDOWS
  struct rlimit limit;
  if (getrlimit(RLIMIT_NOFILE, &limit) < 0) {
    LOG_WARN << "MaximizeFdLimit get open file limit eror.";
    return;
  }
  LOG_DEBUG << "MaximizeFdLimit rlim_cur=" << limit.rlim_cur << " rlim_max=" << limit.rlim_max;

  if (limit.rlim_cur < limit.rlim_max) {
    limit.rlim_cur = limit.rlim_max;
    if (setrlimit(RLIMIT_NOFILE, &limit) < 0) {
      LOG_WARN << "MaximizeFdLimit set RLIMIT_NOFILE fail";
    }
  }
#endif // _WINDOWS
}

int main() {
  base::InitLogging("proxy.log", "TRACE");
  // loguru::g_stderr_verbosity = 0;
  // loguru::g_stderr_verbosity = -8;
  // loguru::g_stderr_verbosity = 8;
  // loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;

  MaximizeFdLimit();

  // TODO : maxmize fd ulimit
  std::string endpoint("127.0.0.1:11311");
  LOG_INFO << "Service listening on " << endpoint;
  yarmproxy::ProxyServer server(endpoint); // TODO : concurrency
  server.Run();
  return 0;
}

