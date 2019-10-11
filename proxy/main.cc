#include "config.h"
#include "logging.h"
#include "proxy_server.h"

namespace yarmproxy {
void Welcome();
int Daemonize();
int MaximizeFdLimit();
int CreatePidFile();
int CleanupPidFile();
}

int main(int argc, char* argv[]) {
  using namespace yarmproxy;
  Welcome();
  auto& conf = Config::Instance();
  const char* conf_file = argc > 1 ? argv[1] : "./yarmproxy.conf";
  if (!conf.Initialize(conf_file)) {
    return 1;
  }
  LOG_INIT(conf.log_file().c_str(), conf.log_level().c_str());

  if (conf.daemonize()) {
    Daemonize();
  }
  MaximizeFdLimit();
  MaximizeFdLimit();
  CreatePidFile();

  LOG_ERROR << "YarmProxy listening on " << conf.listen();
  ProxyServer server(conf.listen(), conf.worker_threads());
  server.Run();
  CleanupPidFile();
  LOG_ERROR << "YarmProxy stopped.";
  return 0;
}

