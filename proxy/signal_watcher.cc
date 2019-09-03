#include "signal_watcher.h"

namespace yarmproxy {

SignalWatcher& SignalWatcher::Instance() {
  static SignalWatcher instance;
  return instance;
}

SignalWatcher::SignalWatcher() {
  sigaction_.sa_handler = SignalWatcher::HandlerEntry;
  sigaction_.sa_flags = 0;
}

void SignalWatcher::OnSignal(int signal) {
  const auto it = signal_handlers_.find(signal);
  if (it != signal_handlers_.end()) {
    for(auto& func : it->second) {
      func(signal);
    }
  }
}

void SignalWatcher::HandlerEntry(int signal) {
  SignalWatcher::Instance().OnSignal(signal);
}

// void SignalWatcher::RegisterHandler(int signal, std::function<void(int)>&& handler) {
void SignalWatcher::RegisterHandler(int signal, const std::function<void(int)>& handler) {
  const auto it = signal_handlers_.find(signal);
  if (it == signal_handlers_.end()) {
    sigaction(signal, &sigaction_, NULL);
    signal_handlers_[signal].emplace_back(handler);
  } else {
    it->second.emplace_back(handler);
  }
}

}
 
