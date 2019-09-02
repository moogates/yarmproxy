#include "proxy_server.h"
#include "logging.h"

#ifndef _WINDOWS
#include <sys/resource.h>
#include <unistd.h>
#endif // _WINDOWS

// TODO : config argv
// TODO : timeout handling
// TODO : max backend conn resource limit
// TODO : refining hash & katama
// TODO : gracefully shutdown
// TODO : kill -HUP reload

int Daemonize() {
#ifndef _WINDOWS  // TODO : 更标准的做法是怎样的?
    switch (fork()) {
    case -1:
        return -1;
    case 0:
        break;
    default:
        _exit(EXIT_SUCCESS);
    }

    if (setsid() == -1)
        return -1;
    int fd = open("/dev/null", O_RDWR, 0);
    if (fd != -1) {
        if(dup2(fd, STDIN_FILENO) < 0) {
            LOG_WARN << "dup2 stdin error";
            return -1;
        }
        if(dup2(fd, STDOUT_FILENO) < 0) {
            LOG_WARN << "dup2 stdout error";
            return -1;
        }
        if(dup2(fd, STDERR_FILENO) < 0) {
            LOG_WARN << "dup2 stderr error";
            return -1;
        }

        if (fd > STDERR_FILENO) {
            if(close(fd) < 0) {
                LOG_WARN << "close fd error";
                return -1;
            }
        }
    }
#endif // _WINDOWS
    return 0;
}

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
  // Daemonize();

  std::string endpoint("127.0.0.1:11311");
  LOG_INFO << "Service listening on " << endpoint;
  yarmproxy::ProxyServer server(endpoint); // TODO : concurrency
  server.Run();
  return 0;
}

