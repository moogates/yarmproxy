#include "config.h"

#include <iostream>
#include <fstream>
#include <vector>

#include <arpa/inet.h>
#include "backend_locator.h"

namespace yarmproxy {

bool Config::ApplyTokens(const std::vector<std::string>& tokens) {
  if (context_.empty()) {
    return ApplyGlobalTokens(tokens);
  }
  if (strncmp(context_.c_str(), "/cluster", sizeof("/cluster") - 1) == 0) {
    return ApplyClusterTokens(tokens);
  }
  if (context_ == "/worker") {
    return ApplyWorkerTokens(tokens);
  }
//if (context_ == "/log") {
//  return ApplyLogTokens(tokens);
//}
  error_msg_ = "unknown context ";
  error_msg_.append(context_);
  return false;
}

void Config::PushSubcontext(const std::string& subcontext) {
  std::cout << "PushSubcontext. pre-context_=" << context_ << std::endl;
  context_.push_back('/');
  context_.append(subcontext);
  std::cout << "PushSubcontext. context_=" << context_ << std::endl;
}
void Config::PopSubcontext() {
  auto const pos = context_.find_last_of('/');
  context_ = context_.substr(0, pos);
  std::cout << "PopSubcontext. context_=" << context_ << std::endl;
}

bool Config::ApplyGlobalTokens(const std::vector<std::string>& tokens) {
  if (tokens.size() == 2 && tokens[0] == "listen") {
    listen_ = tokens[1];
    return true;
  }

  if (tokens.size() == 2 && tokens[0] == "daemonize") {
    daemonize_ = tokens[1] == "true" || tokens[1] == "yes" ||
                 tokens[1] == "1";
    return true;
  }

  if (tokens.size() == 2 && tokens[0] == "worker_threads") {
    try {
      worker_threads_ = std::stoi(tokens[1]);
      return true;
    } catch (...) {
      error_msg_ = "positive integer required";
      return false;
    }
  }

  if (tokens.size() == 2 && tokens[0] == "max_namespace_length") {
    try {
      max_namespace_length_ = std::stoi(tokens[1]);
      if (max_namespace_length_ > 0) {
        return true;
      }
    } catch (...) {}
    error_msg_ = "positive integer required";
    return false;
  }

  if (tokens.size() == 2 && tokens[0] == "log_file") {
    log_file_ = tokens[1];
    return true;
  }

  if (tokens.size() == 2 && tokens[0] == "log_level") {
    log_level_ = tokens[1];
    return true;
  }

  if (tokens.size() == 2 && tokens[0] == "cluster" && tokens[1] == "{") {
    clusters_.emplace_back();
    PushSubcontext(tokens[0]);
    return true;
  }

  if (tokens.size() == 2 && tokens[0] == "worker" && tokens[1] == "{") {
    PushSubcontext(tokens[0]);
    return true;
  }

//if (tokens.size() == 2 && tokens[1] == "{") {
//  PushSubcontext(tokens[0]);
//  return true;
//}
  error_msg_ = "unknown directive ";
  error_msg_.append(tokens[0]);
  return false;
}

bool Config::ApplyWorkerTokens(const std::vector<std::string>& tokens) {
  if (tokens.size() == 1 && tokens[0] == "}") {
    if (context_.empty()) {
      return false;
    }
    PopSubcontext();
    return true;
  }
  if (tokens.size() != 2) {
    error_msg_ = "bad token count";
    return false;
  }

  if (tokens[0] == "max_idle_backends") {
    try {
      worker_max_idle_backends_ = std::stoi(tokens[1]);
    } catch (...) {
      error_msg_ = "bad nubmer";
      return false;
    }
    return true;
  } else if (tokens[0] == "cpu_affinity") {
    // TODO
    return true;
  } else if (tokens[0] == "buffer_size") {
    try {
      int sz = std::stoi(tokens[1]);
      if (sz < 1 || sz > 1024 ||
          ((sz & (sz - 1)) != 0)) {
        error_msg_ = "bad buffer size";
        return false;
      }
      worker_buffer_size_ = sz * 1024;
    } catch (...) {
      error_msg_ = "bad number";
      return false;
    }
    return true;
  } else if (tokens[0] == "buffer_trunk_size") {
    try {
      int sz = std::stoi(tokens[1]);
      if (sz < 32 || sz > 8192 ||
          ((sz & (sz - 1)) != 0)) {
        error_msg_ = "bad buffer trunk size";
        return false;
      }
      worker_buffer_trunk_size_ = sz * 1024;
    } catch (...) {
      error_msg_ = "bad number";
      return false;
    }
    return true;
  }
  error_msg_ = "unknown directive";
  return false;
}

bool Config::ApplyClusterTokens(const std::vector<std::string>& tokens) {
  if (tokens.size() == 1 && tokens[0] == "}") {
    if (context_.empty()) {
      return false;
    }
    PopSubcontext();
    return true;
  }

  if (tokens[0] == "protocol") {
    if (tokens.size() == 2) {
      if (tokens[1] == "redis") {
        clusters_.back().protocol_ = ProtocolType::REDIS;
        return true;
      } else if (tokens[1] == "memcached") {
        clusters_.back().protocol_ = ProtocolType::MEMCACHED;
        return true;
      } else {
        error_msg_ = "unsupported protocol type";
        return false;
      }
    }
  } else if (tokens[0] == "namespace") {
    if (tokens.size() >= 2) {
      for(size_t i = 1; i < tokens.size(); ++i) {
        auto& ns = tokens[i];
        if (ns.size() > max_namespace_length_) {
          error_msg_ = "too long namespace length";
          return false;
        }
      }
      std::copy(tokens.begin() + 1, tokens.end(),
          std::back_inserter(clusters_.back().namespaces_));
      return true;
    }
  } else if (tokens[0] == "backends") {
    if (tokens.size() == 2 && tokens[1] == "{") {
      PushSubcontext(tokens[0]);
      return true;
    }
  } else if (tokens[0] == "backend") {
    std::cout << "backend directive in context " << context_ << std::endl;
    if (context_ == "/cluster/backends") {
      if (tokens.size() == 3) {
        size_t pos = tokens[1].find_first_of(':');
        if (pos == std::string::npos) {
          error_msg_ = "endpoint bad format";
          return false;
        }
        std::string host = tokens[1].substr(0, pos);
        int port = 0;
        int weight = 0;
        try {
          boost::asio::ip::make_address_v4(host);
          port = std::stoi(tokens[1].substr(pos + 1));
        } catch (...) {
          error_msg_ = "bad endpoint";
          return false;
        }
        try {
          weight = std::stoi(tokens[2]);
        } catch (...) {
          error_msg_ = "bad weight";
          return false;
        }
        if (port <= 0 || weight <= 0) {
          error_msg_ = "illegal value";
          return false;
        }
        clusters_.back().backends_.emplace_back(
            std::move(host), port, weight);
        return true;
      }
    } else {
      error_msg_ = "backend must in context #cluster#backends";
    }
  }

  return false;
}

bool Config::ApplyLogTokens(const std::vector<std::string>& tokens) {
  if (tokens.size() == 1 && tokens[0] == "}") {
    PopSubcontext();
    return true;
  }
  return true;
}

bool Config::ReloadCulsters() {
  clusters_.clear();
  return TraverseConfFile(
      [this](const std::vector<std::string>& tokens) -> bool {
          if (context_.empty()) {
            if (tokens[0] != "cluster") {
              std::cout << "skip global directive " << tokens[0] << std::endl;
              return true;
            }
          } else {
            if (strncmp(context_.c_str(), "/cluster", sizeof("/cluster") - 1) != 0) {
               std::cout << "skip none-cluster sub-directive " << tokens[0] << std::endl;
               return true;
            }
          }

          return ApplyTokens(tokens);
      });
}

bool Config::Initialize() {
  return TraverseConfFile(
      [this](const std::vector<std::string>& tokens) -> bool {
          return this->ApplyTokens(tokens);
      });
}

static void TokenizeLine(const std::string& line, std::vector<std::string>* tokens) {
  int status = 0;
  for(char ch : line) {
    if (ch == ';' || ch == '#') {
      return;
    }
    if (status == 0) {
      if (isspace(ch)) {
        continue;
      } else {
        tokens->emplace_back(1, ch);
        status = 1;
      }
    } else {
      if (isspace(ch)) {
        status = 0;
        continue;
      } else {
        tokens->back().push_back(ch);
      }
    }
  }
}

bool Config::TraverseConfFile(TokensHandler handler) {
  std::ifstream conf_fs(config_file_);
  if (!conf_fs) {
    std::cerr << "Open conf file " << config_file_ << " error." << std::endl;
    return false;
  }

  std::string line;
  size_t line_count = 0;
  std::string context;
  while(std::getline(conf_fs, line)) {
    std::vector<std::string> tokens;
    TokenizeLine(line, &tokens);
    line_count++;
    if (tokens.empty()) {
      continue;
    }
    if (!handler(tokens)) {
      std::cout << "Config " << config_file_ << " line " << line_count
                << " error:" << error_msg_ << std::endl;
      return false;
    }
  }
  return true;
}

}


