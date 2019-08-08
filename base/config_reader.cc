#include "config_reader.h"

#include <fstream>

#include <boost/algorithm/string.hpp>

ConfigReader::ConfigReader() : status_(1) {
}

ConfigReader::ConfigReader(const std::string & file_name) : status_(1) {
  Load(file_name);
}

bool ConfigReader::Load(const std::string & file_name) {
  std::ifstream file(file_name.c_str());
  if (!file) {
    LOG_WARN << "open config file error " << file_name;
    return false;
  }
  status_ = 0;

  std::string line;
  std::string cur_section;
  while (std::getline(file,line)) {
    boost::trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';')
      continue;

    if (line[0] == '[') {
      if (line[line.size() - 1] != ']') {
        LOG_WARN << "bad line " << line;
        continue;
      }
      cur_section = line.substr(1, line.size() - 2);
      boost::trim(cur_section);
      continue;
    }

    size_t pos = line.find('=');
    if (pos == std::string::npos) {
      LOG_WARN << "bad line " << line << ", \"=\" required";
      continue;
    }
    std::string key = line.substr(0, pos);
    boost::trim(key);

    std::string value = line.substr(pos + 1);
    boost::trim(value);

    content_[GetSectionkey(cur_section, key)] = value;
  }
  return true;
}

const std::string & ConfigReader::Get(const std::string & section, const std::string & entry) const {
  std::map<std::string, std::string>::const_iterator it = content_.find(GetSectionkey(section, entry));
  if (it != content_.end()) {
    return it->second; 
  }
  static std::string empty;
  return empty;
}

