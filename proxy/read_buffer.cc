#include "read_buffer.h"

#include "base/logging.h"

namespace mcproxy {

size_t ReadBuffer::unparsed_bytes() const {
  LOG_DEBUG << "ReadBuffer::unparsed_bytes pushed="
              << received_bytes_ << " parsed=" << parsed_bytes_;
  if (received_bytes_ > parsed_bytes_) {
    return received_bytes_ - parsed_bytes_;
  }
  return 0;
}

void ReadBuffer::update_processed_bytes(size_t processed) {
  processed_bytes_ += processed;
  try_free_buffer_space();
}

void ReadBuffer::update_received_bytes(size_t received) {
  received_bytes_ += received;
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
    LOG_INFO << "in try_free_buffer_space(), memmove is unlocked, try moving offset, PRE: begin=" << processed_bytes_
             << " end=" << received_bytes_ << " parsed=" << parsed_bytes_;
    if (processed_bytes_ == received_bytes_) {
      parsed_bytes_ -= processed_bytes_;
      processed_bytes_ = received_bytes_ = 0;
    } else if (processed_bytes_ > (BUFFER_SIZE - received_bytes_)) {
      // TODO : memmove
      memmove(data_, data_ + processed_bytes_, received_bytes_ - processed_bytes_);
      parsed_bytes_ -= processed_bytes_;
      received_bytes_ -= processed_bytes_;
      processed_bytes_ = 0;
    }
    LOG_DEBUG << "in try_free_buffer_space(), memmove is unlocked, try moving offset, POST: begin=" << processed_bytes_
             << " end=" << received_bytes_ << " parsed=" << parsed_bytes_;
  } else {
    LOG_DEBUG << "in try_free_buffer_space, memmove is locked, do nothing, memmove_lock_count_=" << memmove_lock_count_;
  }
}

}

