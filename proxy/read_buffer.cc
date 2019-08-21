#include "read_buffer.h"

#include <cstring>

#include "base/logging.h"

namespace mcproxy {

size_t ReadBuffer::unparsed_bytes() const {
  if (received_offset_ > parsed_offset_) {
    return received_offset_ - parsed_offset_;
  }
  return 0;
}

void ReadBuffer::update_processed_bytes(size_t processed) {
  processed_offset_ += processed;
  try_free_buffer_space();
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
  try_free_buffer_space(); // TODO : 这里不释放? 如果需要，可以自己调用
}

void ReadBuffer::try_free_buffer_space() {
  if (memmove_lock_count_ == 0) {
    LOG_INFO << "in try_free_buffer_space(), memmove is unlocked, try moving offset, PRE: begin=" << processed_offset_
             << " end=" << received_offset_ << " parsed=" << parsed_offset_;
    if (processed_offset_ == received_offset_) {
      parsed_offset_ -= processed_offset_;
      processed_offset_ = received_offset_ = 0;
    } else if (processed_offset_ > (BUFFER_SIZE - received_offset_)) {
      // TODO : memmove
      memmove(data_, data_ + processed_offset_, received_offset_ - processed_offset_);
      parsed_offset_ -= processed_offset_;
      received_offset_ -= processed_offset_;
      processed_offset_ = 0;
    }
    LOG_DEBUG << "in try_free_buffer_space(), memmove is unlocked, try moving offset, POST: begin=" << processed_offset_
             << " end=" << received_offset_ << " parsed=" << parsed_offset_;
  } else {
    LOG_DEBUG << "in try_free_buffer_space, memmove is locked, do nothing, memmove_lock_count_=" << memmove_lock_count_;
  }
}

}

