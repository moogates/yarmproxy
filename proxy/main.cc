#include "config.h"
#include "logging.h"
#include "proxy_server.h"

namespace yarmproxy {

void Welcome();
int Daemonize();
int MaximizeFdLimit();

}

int main(int argc, char* argv[]) {
  yarmproxy::Welcome();
  auto& conf = yarmproxy::Config::Instance();
  if (argc > 1) {
    conf.set_config_file(argv[1]);
  }
  if (!conf.Initialize()) {
    return 1;
  }
  LOG_INIT(conf.log_file().c_str(), conf.log_level().c_str());

  if (conf.daemonize()) {
    yarmproxy::Daemonize();
  }
  yarmproxy::MaximizeFdLimit();

  LOG_ERROR << "Service listening on " << conf.listen();
  yarmproxy::ProxyServer server(conf.listen(), conf.worker_threads());
  server.Run();
  return 0;
}

