#ifndef _YARMPROXY_REDIS_PROTOCOL_H_
#define _YARMPROXY_REDIS_PROTOCOL_H_

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cassert>

#include "logging.h"

namespace yarmproxy {
namespace redis {

const int SIZE_PARSE_ERROR = -10001;
const int SIZE_NIL_BULK    = -1;

// light-weighted redis RESP data wrapper and parser

// Bulk:
// "result" -> "$6\r\nresult\r\n"
// ""  -> "$0\r\n\r\n"
// nil  -> "$-1\r\n" (null bulk string)
class Bulk {
public:
  // > 0  -> ok
  // == 0 -> no ready
  // < 0  -> error

  // TODO : 先检查格式正确性
  Bulk(const char* data, size_t bytes)
      : raw_data_(data) {
    if (bytes < 4) {
      present_size_ = 0;
      return;
    }

    const char* p = data;
    if (*p != '$') {
      present_size_ = SIZE_PARSE_ERROR;
      return;
    }
    ++p;
    if (*p == '-') { // nil bulk
      if (p[1] != '1') {
        present_size_ = SIZE_PARSE_ERROR;
        return;
      } else {
        if (bytes < 5) {
          present_size_ = 0;
          return;
        }
        if (p[2] != '\r' || p[3] != '\n') {
          present_size_ = SIZE_PARSE_ERROR;
          return;
        }
        present_size_ = 5;
        return;
      }
    }
    size_t total = 0;
    while(*p != '\r' && size_t(p - data) < bytes) {
      total = total * 10 + size_t(*p - '0');
      ++p;
    }
    if (*p == '\r' && (p + 1 < data + bytes) && p[1] == '\n') {
      present_size_ = std::min(bytes, (p - data) + total + 4);
    } else {
      present_size_ = 0;
    }
  }

  const char* raw_data() const {
    return raw_data_;
  }

  int present_size() const {
    return present_size_;
  }

  size_t total_size() const {
    const char* p = raw_data_ + 1;
    size_t total = 0;
    if (*p == '-') {
      return 5; // nil bulk
    }

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
    try {
      int sz = std::stoi(raw_data_ + 1);
      return sz > 0 ? sz : 0;
    } catch (...) {
      LOG_WARN << "stoi payload_size error, prefix=["
               << std::string(raw_data_, 5) << "]";
      return 0;
    }
  }

  bool equals(const char* str, size_t size) const {
    assert(absent_size() == 0);
    if (size != payload_size()) {
      return false;
    }
    const char* p = str;
    const char* q = payload_data();
    while(size_t(p - str) < size) {
      if (*p++ != *q++) {
        return false;
      }
    }
    return true;
  }

 // ignoring case compare
  bool iequals(const char* str, size_t size) const {
    assert(absent_size() == 0);
    if (size != payload_size()) {
      return false;
    }
    const char* p = str;
    const char* q = payload_data();
    while(size_t(p - str) < size) {
      if (std::tolower(*p++) != std::tolower(*q++)) {
        return false;
      }
    }
    return true;
  }

  bool completed() const {
    return present_size_ > 0 && absent_size() == 0;
  }

  size_t absent_size() const {
    size_t total = total_size();
    return (size_t)present_size_ >= total ?
               0 : (total - (size_t)present_size_);
  }
  std::string to_string() const {
    if (raw_data_[1] == '-') {
      return "nil";
    }
    return std::string(payload_data(),
        total_size() - (payload_data() - raw_data_) - absent_size() - 2);
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
  static std::string SerializePrefix(int i) {
    std::ostringstream oss;
    oss << '*' << i << "\r\n";
    return oss.str();
  }
  static int ParseBulkItems(const char* data, size_t bytes, size_t max_bulks,
                            std::vector<Bulk>* items) {
    const char* p = data;
    int parsed_bytes = 0;
    while(p < data + bytes && items->size() < max_bulks) {
      items->emplace_back(p, data + bytes - p);
      Bulk& back = items->back();
      if (back.present_size() < 0) {
        return -1;
      }
      if (back.present_size() == 0) {
        items->pop_back();
        break;
      }
      parsed_bytes += back.total_size();
      p += back.total_size();
    }

    return parsed_bytes;
  }

  BulkArray(const char* data, size_t bytes)
      : raw_data_(data) {
    if (bytes < 4) {
      parsed_size_ = 0;
      return;
    }
    const char* p = data;
    if (*p != '*') {
      parsed_size_ = SIZE_PARSE_ERROR;
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
      parsed_size_ = SIZE_PARSE_ERROR;
      return;
    }
    p += 2;
    parsed_size_ = p - data;

    int ret = ParseBulkItems(p, data + bytes - p, bulks, &items_);
    if (ret < 0) {
      parsed_size_ = SIZE_PARSE_ERROR;
    } else {
      parsed_size_ += ret;
    }
  }

  const char* raw_data() const {
    return raw_data_;
  }

  Bulk& operator[](size_t pos) {
    return items_[pos];
  }
  const Bulk& operator[](size_t pos) const {
    return items_[pos];
  }

  Bulk& back() {
    return items_.back();
  }

  int parsed_size() const {
    return parsed_size_;
  }

  bool completed() const {
    if (items_.size() != total_bulks()) {
      return false;
    }
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
    try {
      return std::stoi(raw_data_ + 1);
    } catch (...) {
      LOG_WARN << "stoi total_bulks error, prefix=[" << std::string(raw_data_, 5) << "]";
      return 0;
    }
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
  std::vector<Bulk> items_;
};

class Integer {
public:
  static std::string SerializeInt(int i) {
    std::ostringstream oss;
    oss << ':' << i << "\r\n";
    return oss.str();
  }
};

}
}

#endif // _YARMPROXY_REDIS_PROTOCOL_H_

