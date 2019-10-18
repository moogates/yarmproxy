#include <sys/resource.h>

#include <boost/asio.hpp>

#include "../proxy/logging.h"
#include "conn_keeper.h"

int MaximizeFdLimit() {
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
        LOG_DEBUG << "MaximizeFdLimit set ok, min=" << min << " max=" << max; // TODO : check on linux
        min = lim.rlim_cur;
      }
    } while (min + 1 < max);
  } else {
    LOG_DEBUG << "MaximizeFdLimit no set, rlim_cur =" << lim.rlim_cur << " rlim_max=" << lim.rlim_max;
  }
#endif // _WINDOWS
  return 0;
}

int main(int argc, char **argv) {
  // base::InitLogging("proxy.log", "WARN");
  loguru::g_stderr_verbosity = 8;

  using namespace yarmproxy;
  MaximizeFdLimit();
  boost::asio::io_service io_service;

  std::string host = "127.0.0.1";
  // int port = 6379;
  int port = 11311;
  size_t concurrency = 5000;
  LOG_INFO << "connect endpoint " << host<< ":" << port;

  ConnectionKeeper conn_keeper(io_service, host, port, concurrency);
  conn_keeper.Start();

  for (;;) {
    try {
      io_service.run();
      LOG_WARN << "io_service stoped.";
    } catch (std::exception& e) {
      LOG_FATAL << "io_service exception:" << e.what();
    }
  }

  return 0;
}

