#ifndef _YARMPROXY_SIGNAL_WATCHER_H_
#define _YARMPROXY_SIGNAL_WATCHER_H_

#include <functional>
#include <map>
#include <vector>

#include <signal.h>

namespace yarmproxy {

class SignalWatcher {
public:
  static SignalWatcher& Instance();
  void RegisterHandler(int signal, const std::function<void(int)>& handler);
private:
  static void HandlerEntry(int signal);

  SignalWatcher();
  void OnSignal(int signal);
  struct sigaction sigaction_;
  std::map<int, std::vector<std::function<void(int)>>> signal_handlers_;
};

}

#endif // _YARMPROXY_SIGNAL_WATCHER_H_

