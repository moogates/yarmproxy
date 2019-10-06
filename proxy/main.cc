#include "base/logging.h"

#include "proxy_server.h"
#include "config.h"

int Daemonize();
int MaximizeFdLimit();

int main(int argc, char* argv[]) {
  auto& conf = yarmproxy::Config::Instance();
  if (argc > 1) {
    conf.set_config_file(argv[1]);
  }
  if (!conf.Initialize()) {
    return 1;
  }
  base::InitLogging(conf.log_file().c_str(),
                    conf.log_level().c_str());

  if (conf.daemonize()) {
    Daemonize();
  }
  MaximizeFdLimit();

  LOG_ERROR << "Service listening on " << conf.listen();
  yarmproxy::ProxyServer server(conf.listen(), conf.worker_threads());
  server.Run();
  return 0;
}

