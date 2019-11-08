#include "read_buffer.h"

#include <cstring>

#include "logging.h"

namespace yarmproxy {

size_t ReadBuffer::unparsed_bytes() const {
  if (received_offset_ > parsed_offset_) {
    return received_offset_ - parsed_offset_;
  }
  return 0;
}

ReadBuffer::~ReadBuffer() {
}

void ReadBuffer::update_received_bytes(size_t received_bytes) {
  received_offset_ += received_bytes;
}

void ReadBuffer::push_reply_data(const char* data, size_t bytes, bool parsed) {
  memcpy(data_, data, bytes);
  received_offset_ += bytes;
  if (parsed) {
    parsed_offset_ += bytes;
  }
}

size_t ReadBuffer::unprocessed_bytes() const {
  return std::min(received_offset_, parsed_offset_) - processed_offset_;
}

void ReadBuffer::update_processed_offset(size_t processed) {
  processed_offset_ += processed;
}
void ReadBuffer::update_processed_bytes(size_t processed) {
  processed_offset_ += processed;
  try_recycle_buffer();
}

bool ReadBuffer::recycle_locked() const {
  return recycle_lock_count_ > 0;
}

void ReadBuffer::inc_recycle_lock() {
  ++recycle_lock_count_;
}
void ReadBuffer::dec_recycle_lock() {
  assert(recycle_lock_count_ > 0);
  if (recycle_lock_count_ > 0) {
    --recycle_lock_count_;
  }
  try_recycle_buffer();
}

void ReadBuffer::try_recycle_buffer() {
  if (recycle_lock_count_ == 0) {
    if (processed_offset_ == received_offset_) {
      parsed_offset_ -= processed_offset_;
      processed_offset_ = received_offset_ = 0;
    } else if (processed_offset_ > (buffer_size_ - received_offset_)) {
      memmove(data_, data_ + processed_offset_,
              received_offset_ - processed_offset_);
      assert(parsed_offset_ >= processed_offset_);
      parsed_offset_ -= processed_offset_;
      received_offset_ -= processed_offset_;
      processed_offset_ = 0;
    }
  }
}

}

