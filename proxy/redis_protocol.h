#ifndef _YARMPROXY_REDIS_PROTOCOL_H_
#define _YARMPROXY_REDIS_PROTOCOL_H_

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "logging.h"

namespace yarmproxy {
namespace redis {

const size_t PARSE_ERROR = INT_MAX;

// Bulk:
// "result" -> "$6\r\nresult\r\n"
// ""  -> "$0\r\n\r\n"
// nil  -> "$-1\r\n" (null bulk string)
class Bulk {
public:
  // > 0  -> ok
  // == 0 -> no ready
  // < 0  -> error
  static int Check(const char* data, size_t bytes) {
    if (bytes < 4) {
      return 0;
    }
    const char* p = data;
    if (*p != '$') {
      return -1;
    }
    for(++p; *p != '\n' && p - data < bytes; ++p) {
      ;
    }
    return *p == '\n' ? 1 : 0;
  }

  // TODO : 先检查格式正确性
  Bulk(const char* data, size_t bytes) 
      : raw_data_(data) {
    if (bytes < 4) {
      present_size_ = 0;
      return;
    }

    const char* p = data;
    if (*p != '$') {
      present_size_ = -1;
      return;
    }
    for(++p; *p != '\n' && p - data < bytes; ++p) {
      ;
    }
    present_size_ = (*p == '\n' ? bytes : 0);
  }

  const char* raw_data() const {
    return raw_data_;
  }

  size_t present_size() const {
    return present_size_;
  }

  size_t total_size() const {
    const char* p = raw_data_ + 1;
    size_t total = 0;
    while(*p != '\r') {
      total = total * 10 + size_t(*p - '0');
      ++p;
    }
    return total + size_t(p - raw_data_) + 4;
  }

  const char* payload_data() const {
    const char* p = raw_data_ + 2;
    while(*p != '\n') {
      ++p;
    }
    return p + 1;
  }
  size_t payload_size() const {
    return std::stoi(raw_data_ + 1);
  }

  bool equals(const char* str, size_t len) const {
    assert(absent_size() == 0);
    if (len != payload_size()) {
      return false;
    }
    const char* p = str;
    const char* q = payload_data();
    while(p - str < len) {
      if (*p++ != *q++) {
        return false;
      }
    }
    return true;
  }

  bool completed() const {
    return absent_size() == 0; 
  }

  size_t absent_size() const {
    size_t total = total_size();
    LOG_DEBUG << "Bulk absent_size() present_size_="<< present_size_
              << " total=" << total;
    return present_size_ >= total ? 0 : (total - present_size_);
  }
  std::string to_string() const {
    return std::string(payload_data(), payload_size() - absent_size());
  }
private:
  const char* raw_data_;
  int present_size_;
};


// Bulk Array
// [] -> "*0\r\n"
// ["foo", "bar"] -> "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n"
// [1, 2, 3] -> "*3\r\n:1\r\n:2\r\n:3\r\n"
// [1, 2, "hello"]  ->  "*3\r\n:1\r\n:2\r\n+hello\r\n" (array with mixed types)
class BulkArray {
public:
  static int Check(const char* data, size_t bytes) {
    const char* p = data;
    if (*p != '*') {
      return -1;
    }
    for(++p; *p != '\n' && p - data < bytes; ++p) {
      ;
    }
    if (*p != '\n') {
      return 0;
    }
    return Bulk::Check(p + 1, data + bytes - p); // TODO check empty array
  }

  BulkArray(const char* data, size_t bytes) 
      : raw_data_(data) {
    const char* p = data;
    if (*p != '*') {
      parsed_size_ = -1;
      return;
    }
    size_t bulks = 0;
    for(++p; p < data + bytes && isdigit(*p); ++p) {
      bulks = bulks * 10 + size_t(*p - '0');
    }
    if (p + 2 > data + bytes) {
      parsed_size_ = 0;
      return;
    }
    if (p == data + 1 || p[0] != '\r' || p[1] != '\n') { // TODO : is strict check required here?
      parsed_size_ = -1;
      return;
    }
    parsed_size_ = p - data + 2;
    for(p += 2; p < data + bytes && items_.size() < bulks; p += items_.back().total_size()) {
      items_.emplace_back(p, data + bytes - p);
      Bulk& back = items_.back();
      if (back.present_size() < 0) {
        parsed_size_ = -1;
        return;
      }
      if (back.present_size() == 0) {
        return;
      }
      parsed_size_ += back.total_size();
      p += back.total_size();
      LOG_DEBUG << "BulkArray ctor, p-data= "<< int(p - data)
              << " total_bulks=" << bulks
              << " item_[" << items_.size() - 1 << "].total_size=" << back.total_size()
              << " item_[" << items_.size() - 1 << "].present_size=" << back.present_size()
              << " item_[" << items_.size() - 1 << "].completed=" << back.completed()
              << " item_[" << items_.size() - 1 << "].payload=" << back.to_string()
              << " item_[" << items_.size() - 1 << "].payload_size=" << back.payload_size();
    }
  }

  Bulk& operator[](size_t pos) {
    return items_[pos];
  }

  Bulk& back() {
    return items_.back();
  }

  size_t parsed_size() const {
    return parsed_size_;
  }

  bool completed() const {
    LOG_DEBUG << "BulkArray completed items_.size="<< items_.size()
              << " total_bulks=" << total_bulks();
    if (items_.size() != total_bulks()) {
      return false;
    }
    LOG_DEBUG << "BulkArray completed items_.size="<< items_.size()
              << " back_absent=" << (items_.empty() ? 0 : items_.back().absent_size());
    if (items_.size() > 0 && items_.back().absent_size() > 0) {
      return false;
    }
    return true;
  }

  size_t total_size() const {
    if (items_.size() < total_bulks()) {
      return 0; // incomplete data, unknown
    }
    if (total_bulks() == 0) {
      return 4;
    }

    size_t total = items_.front().raw_data() - raw_data_;
    for(auto& item : items_) {
      total += item.total_size();
    }
    return total;
  }

  size_t total_bulks() const {
    return std::stoi(raw_data_ + 1);
  }
  size_t present_bulks() const {
    return items_.size();
  }
  size_t absent_bulks() const {
    return total_bulks() - present_bulks();
  }
private:
  const char* raw_data_;
  size_t parsed_size_;
  std::vector<Bulk> items_; // TODO :  use std::array
};


//int ParseInteger(const char* data, size_t bytes) {
//  // 1000 -> ":1000\r\n"
//  return 0;
//}

//int ParseErrorString(const char* data, size_t bytes) {
//  // "SERVER_ERROR" -> "-SERVER_ERROR\r\n"
//  return 0;
//}

//int ParseSimpleString(const char* data, size_t bytes) {
//  // "HELLO" -> "+HELLO\r\n"
//  return 0;
//}


}
}

#endif // _YARMPROXY_REDIS_PROTOCOL_H_

