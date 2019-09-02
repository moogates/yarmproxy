#include "proxy_server.h"
#include "logging.h"

#ifndef _WINDOWS
#include <sys/resource.h>
#endif // _WINDOWS

// TODO : daemonize
// TODO : config argv
// TODO : timeout handling
// TODO : max backend conn resource limit
// TODO : refining hash & katama
// TODO : gracefully shutdown

void MaximizeFdLimit() {
#ifndef _WINDOWS
  struct rlimit lim;
  if (getrlimit(RLIMIT_NOFILE, &lim) == 0 && lim.rlim_cur != lim.rlim_max) {
    rlim_t min = lim.rlim_cur;
    rlim_t max = 1 << 20;
    // But if there's a defined upper bound, don't search, just set it.
    if (lim.rlim_max != RLIM_INFINITY) {
      min = lim.rlim_max;
      max = lim.rlim_max;
    }

    // Do a binary search for the limit.
    do {
      lim.rlim_cur = min + (max - min) / 2;
      if (setrlimit(RLIMIT_NOFILE, &lim)) {
        max = lim.rlim_cur;
      } else {
        LOG_INFO << "MaximizeFdLimit set ok, min=" << min << " max=" << max; // TODO : check on linux
        min = lim.rlim_cur;
      }
    } while (min + 1 < max);
  } else {
    LOG_INFO << "MaximizeFdLimit no set, rlim_cur =" << lim.rlim_cur << " rlim_max=" << lim.rlim_max;
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

