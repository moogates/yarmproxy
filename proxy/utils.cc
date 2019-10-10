#include <iostream>

#ifndef _WINDOWS
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#endif // _WINDOWS

#include "logging.h"

#ifdef _GNU_SOURCE
#include <sched.h>
#endif

namespace yarmproxy {

void Welcome() {
  std::cout << "\
 __   __ _    ____  __  __ ____  ____   _____  ___   __ \r\n\
 \\ \\ / // \\  |  _ \\|  \\/  |  _ \\|  _ \\ / _ \\ \\/ \\ \\ / / \r\n\
  \\ V // _ \\ | |_) | |\\/| | |_) | |_) | | | \\  / \\ V /  \r\n\
   | |/ ___ \\|  _ <| |  | |  __/|  _ <| |_| /  \\  | |   \r\n\
   |_/_/   \\_\\_| \\_\\_|  |_|_|   |_| \\_\\\\___/_/\\_\\ |_|   \r\n"
      << std::endl;
}

int SetCpuAffinity(int cpu) {
#ifdef _GNU_SOURCE
  cpu_set_t  mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu, &mask);
  return sched_setaffinity(0, sizeof(mask), &mask);
#else
  return 1;
#endif
}

int Daemonize() {
#ifndef _WINDOWS  // TODO : 更标准的做法是怎样的?
    switch (fork()) {
    case -1:
        return -1;
    case 0:
        break;
    default:
        exit(0);
    }

    if (setsid() == -1) {
      return -1;
    }

    int fd = open("/dev/null", O_RDWR, 0);
    if (fd != -1) {
        if (dup2(fd, STDIN_FILENO) < 0) {
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            return -1;
        }
        if (dup2(fd, STDERR_FILENO) < 0) {
            return -1;
        }

        if (fd > STDERR_FILENO) {
            if(close(fd) < 0) {
                return -1;
            }
        }
    }
#endif // _WINDOWS
    return 0;
}

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

} // namespace yarmproxy

