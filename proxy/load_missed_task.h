#ifndef _LOAD_MISSED_TASK_H_
#define _LOAD_MISSED_TASK_H_

#include <memory>
#include <string>
#include <vector>

namespace mcproxy {
class MemcCommand;

class LoadMissedTask {
public:
  LoadMissedTask(const std::vector<std::string> &keys, std::shared_ptr<MemcCommand> cmd);
  virtual void handle();
private:
  std::vector<std::string> keys_;
  std::shared_ptr<MemcCommand> memc_cmd_;
};

}

#endif // _LOAD_MISSED_TASK_H_
