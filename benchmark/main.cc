#include <sys/resource.h>

#include <boost/asio.hpp>

#include "base/logging.h"
#include "conn_keeper.h"

void UseMaxFdLimit() {
  struct rlimit limit;
  if (getrlimit(RLIMIT_NOFILE, &limit) < 0) {
    LOG_WARN << "get open file limit eror.";
  } else {
    LOG_INFO << "get rlim_cur=" << limit.rlim_cur << " rlim_max=" << limit.rlim_max;
  }
 
  limit.rlim_cur = limit.rlim_max;
  if (setrlimit(RLIMIT_NOFILE, &limit) < 0) {
    LOG_WARN << "set rlim_cur=" << limit.rlim_cur << " rlim_max=" << limit.rlim_max << " err";
  } else {
    LOG_INFO << "set rlim_cur=" << limit.rlim_cur << " rlim_max=" << limit.rlim_max << " ok";
  }
}

int main(int argc, char **argv) {
  // base::InitLogging("proxy.log", "WARN");
  loguru::g_stderr_verbosity = 8;

  using namespace yarmproxy;

  UseMaxFdLimit();

  boost::asio::io_service io_service;

  std::string host = "127.0.0.1";
  // int port = 6379;
  int port = 11311;
  size_t concurrency = 1;
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

