#ifndef __CONFIG_FILE_H__
#define __CONFIG_FILE_H__

#include <string>
#include <map>

#include <boost/lexical_cast.hpp>

#include "logging.h"

class ConfigReader {
 public:
  ConfigReader();
  explicit ConfigReader(const std::string & file);

  bool Load(const std::string & file);

  const std::string & Get(const std::string & section, const std::string & entry) const;

  template <typename T>
  T GetWithType(const std::string & section, const std::string & entry, T default_value) const {
  // int ConfigReader::GetInt(const std::string & section, const std::string & entry, int default_value) const {
    int ret = default_value;
    std::map<std::string, std::string>::const_iterator it = content_.find(GetSectionkey(section, entry));
    if (it != content_.end()) {
      try {
        ret = boost::lexical_cast<T>(it->second);
      } catch (boost::bad_lexical_cast & e) {
        LOG_WARN << "GetWithType() bad_lexical_cast : " << e.what();
      }
    }
    return ret;
  }
  operator bool() const {
    return status_ == 0; 
  }
 private:
  std::map<std::string, std::string> content_;

  static std::string GetSectionkey(const std::string & section, const std::string & entry) {
    if (section.empty()) {
      return entry;
    }
    return section + '/' + entry;
  }
  int status_;
};

#endif

