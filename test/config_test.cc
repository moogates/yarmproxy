#include "config.h"
// #include "base/logging.h"

// #include <cassert>
#include <iostream>

int main() {
  using namespace yarmproxy;
  Config::Instance().Reload();

  std::cout << "daemonize : " << Config::Instance().daemonize() << std::endl;
  return 0;
}
