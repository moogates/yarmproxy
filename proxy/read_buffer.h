#ifndef _READ_BUFFER_H_
#define _READ_BUFFER_H_

#include <algorithm>

#include "base/logging.h"

namespace mcproxy {

class ReadBuffer {
private:
  enum { BUFFER_SIZE = 32 * 1024}; // TODO : use c++11 enum
  char data_[BUFFER_SIZE];

  size_t popped_bytes_;
  size_t pushed_bytes_;
  size_t parsed_bytes_;

  size_t memmove_lock_count_;
public:
  ReadBuffer() : popped_bytes_(0)
               , pushed_bytes_(0)
               , parsed_bytes_(0) 
               , memmove_lock_count_(0) {
  }
public:
  void Reset() {
    pushed_bytes_ = popped_bytes_ = parsed_bytes_ = 0; // TODO : 这里需要吗？
    memmove_lock_count_ = 0;
  }

  bool has_much_free_space() {
    return pushed_bytes_ * 3 <  BUFFER_SIZE * 2; // there is still more than 1/3 buffer space free
  }

  char* free_begin() {
    return data_ + pushed_bytes_;
  }
  size_t free_size() {
    return BUFFER_SIZE - pushed_bytes_;
  }

  void lock_memmove();
  void unlock_memmove();

  const char* to_transfer_data() const { // 可以向下游传递的数据
    return data_ + popped_bytes_;
  }
  size_t to_transfer_bytes() const {
    LOG_DEBUG << "ReadBuffer::to_transfer_bytes pushed="
              << pushed_bytes_ << " parsed=" << parsed_bytes_
              << " popped=" << popped_bytes_;
    return std::min(pushed_bytes_, parsed_bytes_) - popped_bytes_;
  }
  void update_transfered_bytes(size_t transfered);
  void update_received_bytes(size_t transfered);

  void update_parsed_bytes(size_t bytes) {
    parsed_bytes_ += bytes;
  }
  const char * unparsed_data() const {
    return data_ + parsed_bytes_;
  }
  size_t unparsed_bytes() const;

  void try_free_buffer_space();
};

}

#endif // _READ_BUFFER_H_

