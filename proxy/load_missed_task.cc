#include "load_missed_task.h"
#include <boost/algorithm/string.hpp>

#include "memc_command.h"
#include "proxy_server.h"

#include "base/logging.h"

namespace mcproxy {

LoadMissedTask::LoadMissedTask(const std::vector<std::string> & keys, std::shared_ptr<MemcCommand> cmd) 
    : memc_cmd_(cmd) {
  for(size_t i = 0; i < keys.size(); ++i) {
    if (boost::starts_with(keys[i], "FEED#")) {
      keys_.push_back(keys[i]);
      // MCE_DEBUG("key to load: " << keys[i]);
    } else {
      // MCE_DEBUG("key not loaded : " << keys[i]);
    }
  }
}

void LoadMissedTask::handle() {
  std::map<std::string, std::string> res;
  try {
    // res = MemcFeedLoaderAdapter::instance().GetFeedSeqByKey(keys_);
  } catch (...) {
    // MCE_WARN("MemcFeedLoaderAdapter unknown error");
  }

  std::string & buf = memc_cmd_->missed_buf();
  buf.clear();
  for(auto it = res.begin(); it != res.end(); ++it) {
    buf += "VALUE ";
    buf += it->first;

    if (*(it->second.rend()) == 'c') {
      buf += " 1 ";
    } else {
      buf += " 0 ";
    }
    buf += std::to_string(it->second.size() - 1);
    buf += "\r\n";
    buf.append(it->second, 0, it->second.size() - 1);
    buf += "\r\n";
  }
  memc_cmd_->DispatchMissedKeyData();
}

}

