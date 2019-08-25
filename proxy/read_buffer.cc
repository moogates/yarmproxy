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

ReadBuffer::~ReadBuffer() {
  // LOG_INFO << "ReadBuffer dtor";
}

void ReadBuffer::update_received_bytes(size_t received_bytes) {
  // LOG_DEBUG << "ReadBuffer::update_received_bytes, this=" << this
  //           << " received_data=" << std::string(data_ + received_offset_, received_bytes);
  received_offset_ += received_bytes;
  LOG_DEBUG << "ReadBuffer::update_received_bytes, this=" << this << " received=" << received_bytes
           << " new received_offset_=" << received_offset_;
}

size_t ReadBuffer::unprocessed_bytes() const {  // 已经接收，且已经解析，但尚未处理的数据
  size_t ret = std::min(received_offset_, parsed_offset_) - processed_offset_;
  LOG_DEBUG << "ReadBuffer::unprocessed_bytes this=" << this << " received_offset_="
            << received_offset_ << " parsed_offset_=" << parsed_offset_
            << " processed_offset_=" << processed_offset_
            << " ret=" << ret;
  return ret;
}

void ReadBuffer::update_processed_bytes(size_t processed) {
  processed_offset_ += processed;
  LOG_DEBUG << "ReadBuffer::update_processed_bytes, this=" << this << " processed=" << processed
           << " new processed_offset_=" << processed_offset_;
  try_recycle_buffer();
}

// FIXME : lock should begin at read-start-point, finishes at sent-done-point. please recheck.
void ReadBuffer::inc_recycle_lock() {
  ++recycle_lock_count_;
  LOG_DEBUG << "ReadBuffer::inc_recycle_lock, this=" << this << " recycle_lock_count_=" << recycle_lock_count_;
}
void ReadBuffer::dec_recycle_lock() {
  if (recycle_lock_count_ > 0) {
    --recycle_lock_count_;
    LOG_DEBUG << "ReadBuffer::dec_recycle_lock, this=" << this << " recycle_lock_count_=" << recycle_lock_count_;
  } else {
    LOG_DEBUG << "ReadBuffer::dec_recycle_lock, this=" << this << " bad unlock";
  }
  try_recycle_buffer(); // TODO : 这里不释放? 如果需要，可以自己调用
}

void ReadBuffer::try_recycle_buffer() {
  if (recycle_lock_count_ == 0) {
    LOG_DEBUG << "ReadBuffer try_recycle_buffer(), this=" << this << " memmove is unlocked, try moving offset, PRE: begin=" << processed_offset_
             << " end=" << received_offset_ << " parsed=" << parsed_offset_;
    if (processed_offset_ == received_offset_) {
      parsed_offset_ -= processed_offset_;
      processed_offset_ = received_offset_ = 0;
    } else if (processed_offset_ > (buffer_size_ - received_offset_)) {
      memmove(data_, data_ + processed_offset_, received_offset_ - processed_offset_);
      parsed_offset_ -= processed_offset_;
      received_offset_ -= processed_offset_;
      processed_offset_ = 0;
    }
    LOG_DEBUG << "ReadBuffer try_recycle_buffer(), this=" << this << " memmove is unlocked, try moving offset, POST: begin=" << processed_offset_
             << " end=" << received_offset_ << " parsed=" << parsed_offset_;
  } else {
    LOG_DEBUG << "ReadBuffer try_recycle_buffer, this=" << this << " memmove is locked, do nothing, recycle_lock_count_=" << recycle_lock_count_;
  }
}

}

