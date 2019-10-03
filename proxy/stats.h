#ifndef _YAMPROXY_STATS_H_
#define _YAMPROXY_STATS_H_

#include <atomic>
#include <ctime>

namespace yarmproxy {

struct Stats;
extern Stats g_stats_;

struct Stats {
  Stats() : start_since_(time(nullptr)) {
  }
  time_t start_since_;
  std::atomic_int32_t client_conns_;
  std::atomic_int32_t backend_conns_;

  std::atomic_int64_t bytes_from_clients_;
  std::atomic_int64_t bytes_to_clients_;
  std::atomic_int64_t bytes_from_backends_;
  std::atomic_int64_t bytes_to_backends_;
};

}

#endif // _YAMPROXY_STATS_H_
