#include "config.h"

// #include <cassert>
#include <iostream>

int main() {
  using namespace yarmproxy;
  Config::Instance().Initialize("./yarmproxy.conf");

  std::cout << "lisetn    : " << Config::Instance().listen() << std::endl;
  std::cout << "daemonize : " << Config::Instance().daemonize() << std::endl;
  std::cout << "log_file  : " << Config::Instance().log_file() << std::endl;
  std::cout << "log_level : " << Config::Instance().log_level() << std::endl;
  std::cout << "clusters  : " << Config::Instance().clusters().size() << std::endl;
  return 0;
}
