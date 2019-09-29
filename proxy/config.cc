#include "config.h"

#include <iostream>
#include <fstream>
#include <vector>

namespace yarmproxy {

void TokenizeLine(const std::string& line, std::vector<std::string>* tokens) {
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

bool Config::ApplyTokens(const std::vector<std::string>& tokens) {
  if (context_.empty()) {
    return ApplyGlobalTokens(tokens);
  }
  if (strncmp(context_.c_str(), "/cluster", 8) == 0) {
    std::cout << "=============================" << context_ << std::endl;
    return ApplyClusterTokens(tokens);
  }
  if (context_ == "/log") {
    return ApplyLogTokens(tokens);
  }
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
  if (pos == std::string::npos) {
    // TODO : return error on empty
  }
  context_ = context_.substr(0, pos);
  std::cout << "PopSubcontext. context_=" << context_ << std::endl;
}

bool Config::ApplyGlobalTokens(const std::vector<std::string>& tokens) {
  if (tokens.size() == 2 && tokens[0] == "daemonize") {
    set_daemonize(tokens[1] == "true" || tokens[1] == "yes" || tokens[1] == "1");
    return true;
  }

  if (tokens.size() == 2 && tokens[0] == "cluster" && tokens[1] == "{") {
    clusters_.emplace_back();
    PushSubcontext(tokens[0]);
    return true;
  }

  if (tokens.size() == 2 && tokens[1] == "{") {
    PushSubcontext(tokens[0]);
    return true;
  }
  error_msg_ = "unknown token ";
  error_msg_.append(tokens[0]);
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
      }
    }
  } else if (tokens[0] == "namespace") {
    if (tokens.size() >= 2) {
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
        int weight = 0;
        try {
          weight = std::stoi(tokens[2]);
        } catch (...) {
          return false;
        }
        clusters_.back().weighted_backends_.emplace_back(tokens[1], weight);
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

bool Config::Reload() {
  std::string file_name("../proxy/yarmproxy.conf");
  std::ifstream conf_file(file_name);
  if (!conf_file) {
    std::cout << "Open conf file " << file_name << " error." << std::endl;
    return false;
  }

  std::string line;
  size_t line_count = 0;
  std::string context;
  while(std::getline(conf_file, line)) {
    std::vector<std::string> tokens;
    TokenizeLine(line, &tokens);
    std::cout << line_count++ << " ";
    if (tokens.empty()) {
      continue;
    }

    std::copy(tokens.begin(), tokens.end(),
        std::ostream_iterator<std::string>(std::cout, " / "));
    std::cout << std::endl;

    if (!ApplyTokens(tokens)) {
      std::cout << "Config " << file_name << " line " << line_count << " error:" << error_msg_ << std::endl;
      return false;
    }
  }

  conf_file.close();
  return true;
}

}


