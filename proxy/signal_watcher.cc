#include "signal_watcher.h"

namespace yarmproxy {

SignalWatcher& SignalWatcher::Instance() {
  static SignalWatcher instance;
  return instance;
}

SignalWatcher::SignalWatcher() {
#ifndef _WIN32
  sigaction_.sa_handler = SignalWatcher::HandlerEntry;
  sigaction_.sa_flags = 0;
#endif
}


void SignalWatcher::OnSignal(int signal) {
#ifndef _WIN32
  const auto it = signal_handlers_.find(signal);
  if (it != signal_handlers_.end()) {
    for(auto& func : it->second) {
      func(signal);
    }
  }
#endif
}

void SignalWatcher::HandlerEntry(int signal) {
  SignalWatcher::Instance().OnSignal(signal);
}

void SignalWatcher::RegisterHandler(int signal,
    const std::function<void(int)>& handler) {
#ifndef _WIN32
  const auto it = signal_handlers_.find(signal);
  if (it == signal_handlers_.end()) {
    sigaction(signal, &sigaction_, NULL);
    signal_handlers_[signal].emplace_back(handler);
  } else {
    it->second.emplace_back(handler);
  }
#endif
}

}

