#include "read_buffer.h"

#include "base/logging.h"

namespace mcproxy {

size_t ReadBuffer::unparsed_bytes() const {
  LOG_DEBUG << "ReadBuffer::unparsed_bytes pushed="
              << pushed_bytes_ << " parsed=" << parsed_bytes_;
  if (pushed_bytes_ > parsed_bytes_) {
    return pushed_bytes_ - parsed_bytes_;
  }
  return 0;
}

void ReadBuffer::update_transfered_bytes(size_t transfered) {
  popped_bytes_ += transfered;
  try_free_buffer_space();
}

void ReadBuffer::update_received_bytes(size_t received_bytes) {
  pushed_bytes_ += received_bytes;
}

void ReadBuffer::lock_memmove() {
  ++memmove_lock_count_;
  LOG_INFO << "ReadBuffer::lock_memmove, memmove_lock_count_=" << memmove_lock_count_;
}
void ReadBuffer::unlock_memmove() {
  if (memmove_lock_count_ > 0) {
    --memmove_lock_count_;
    LOG_INFO << "ReadBuffer::unlock_memmove, memmove_lock_count_=" << memmove_lock_count_;
  } else {
    LOG_WARN << "ReadBuffer::unlock_memmove, bad unlock";
  }
  return;
  try_free_buffer_space(); // 这里不释放，如果需要，可以自己调用
}

void ReadBuffer::try_free_buffer_space() {
  if (memmove_lock_count_ == 0) {
    LOG_INFO << "in try_free_buffer_space(), memmove is unlocked, try moving offset, PRE: begin=" << popped_bytes_
             << " end=" << pushed_bytes_ << " parsed=" << parsed_bytes_;
    if (popped_bytes_ == pushed_bytes_) {
      parsed_bytes_ -= popped_bytes_;
      popped_bytes_ = pushed_bytes_ = 0;
    } else if (popped_bytes_ > (BUFFER_SIZE - pushed_bytes_)) {
      // TODO : memmove
      memmove(data_, data_ + popped_bytes_, pushed_bytes_ - popped_bytes_);
      parsed_bytes_ -= popped_bytes_;
      pushed_bytes_ -= popped_bytes_;
      popped_bytes_ = 0;
    }
    LOG_DEBUG << "in try_free_buffer_space(), memmove is unlocked, try moving offset, POST: begin=" << popped_bytes_
             << " end=" << pushed_bytes_ << " parsed=" << parsed_bytes_;
  } else {
    LOG_DEBUG << "in try_free_buffer_space, memmove is locked, do nothing, memmove_lock_count_=" << memmove_lock_count_;
  }
}

}

