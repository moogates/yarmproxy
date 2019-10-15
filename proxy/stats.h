#ifndef _YAMPROXY_STATS_H_
#define _YAMPROXY_STATS_H_

#include <atomic>
#include <ctime>

namespace yarmproxy {

struct Stats;
extern Stats g_stats_;

struct Stats {
  Stats() : alive_since_(time(nullptr)) {
  }
  time_t alive_since_;
  std::atomic_int client_conns_;
  std::atomic_int backend_conns_;

  std::atomic_llong bytes_from_clients_;
  std::atomic_llong bytes_to_clients_;
  std::atomic_llong bytes_from_backends_;
  std::atomic_llong bytes_to_backends_;

  std::atomic_llong client_read_timeouts_;
  std::atomic_llong client_write_timeouts_;

  std::atomic_llong backend_connect_errors_;
  std::atomic_llong backend_connect_timeouts_;
  std::atomic_llong backend_read_timeouts_;
  std::atomic_llong backend_write_timeouts_;
};

}

#endif // _YAMPROXY_STATS_H_
