#ifndef _READ_BUFFER_H_
#define _READ_BUFFER_H_

#include <algorithm>

#include "base/logging.h"

namespace mcproxy {

class ReadBuffer {
private:
  enum { BUFFER_SIZE = 32 * 1024}; // TODO : use c++11 enum
  char data_[BUFFER_SIZE];

  size_t processed_bytes_;
  size_t received_bytes_;
  size_t parsed_bytes_;
  size_t memmove_lock_count_;
public:
  ReadBuffer() : processed_bytes_(0)
               , received_bytes_(0)
               , parsed_bytes_(0) 
               , memmove_lock_count_(0) {
  }

  void Reset() {
    received_bytes_ = processed_bytes_ = parsed_bytes_ = 0; // TODO : 这里需要吗？
    memmove_lock_count_ = 0;
  }

  bool has_much_free_space() {
    return received_bytes_ * 3 < BUFFER_SIZE * 2; // there is still more than 1/3 buffer space free
  }

  char* free_space_begin() {
    return data_ + received_bytes_;
  }
  size_t free_space_size() {
    return BUFFER_SIZE - received_bytes_;
  }

  void lock_memmove();
  void unlock_memmove();

  const char* unprocessed_data() const {
    return data_ + processed_bytes_;
  }
  size_t unprocessed_bytes() const {  // 已经接收，且已经解析，但尚未处理的数据
    LOG_DEBUG << "ReadBuffer::unprocessed_bytes pushed="
              << received_bytes_ << " parsed=" << parsed_bytes_
              << " processed=" << processed_bytes_;
    return std::min(received_bytes_, parsed_bytes_) - processed_bytes_;
  }

  void update_processed_bytes(size_t processes_bytes);
  void update_received_bytes(size_t received_bytes);

  void update_parsed_bytes(size_t bytes) {
    parsed_bytes_ += bytes;
  }
  const char * unparsed_data() const {
    return data_ + parsed_bytes_;
  }
  size_t unparsed_bytes() const;  // 尚未解析的数据
private:
  void try_free_buffer_space();
};

}

#endif // _READ_BUFFER_H_

