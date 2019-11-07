#ifndef _YARMPROXY_READ_BUFFER_H_
#define _YARMPROXY_READ_BUFFER_H_

#include <algorithm>
#include <cassert>

namespace yarmproxy {

class ReadBuffer {
private:
  char* data_;
  size_t buffer_size_;

  size_t processed_offset_   = 0;
  size_t received_offset_    = 0;
  size_t parsed_offset_      = 0;
  size_t recycle_lock_count_ = 0;
public:
  ReadBuffer(char* buffer, size_t buffer_size)
      : data_(buffer)
      , buffer_size_(buffer_size) {
  }
  ~ReadBuffer();
  char* data() {
    return data_;
  }

  void Reset() {
    processed_offset_   = 0;
    received_offset_    = 0;
    parsed_offset_      = 0;
    recycle_lock_count_ = 0;
  }

  bool has_much_free_space() {
    // there is more than 1/3 free space
    return received_offset_ * 3 < buffer_size_ * 2;
  }

  char* free_space_begin() {
    return data_ + received_offset_;
  }
  size_t free_space_size() {
    return buffer_size_ - received_offset_;
  }

  size_t received_bytes() const { // received_unprocessed_bytes
    return received_offset_ - processed_offset_;
  }

  void cut_received_tail(size_t bytes) {
    received_offset_ -= bytes;
  }
  void recover_received_tail(size_t bytes) {
    received_offset_ += bytes;
    parsed_offset_ += bytes;
  }

  bool recycle_locked() const;
  void inc_recycle_lock();
  void dec_recycle_lock();

  const char* unprocessed_data() const {
    return data_ + processed_offset_;
  }
  size_t unprocessed_bytes() const;  // received, parsed, and unprocessed

  void update_processed_offset(size_t processed);
  void update_processed_bytes(size_t processes_bytes);
  void update_received_bytes(size_t received_bytes);
  void push_reply_data(const char* data, size_t bytes, bool parsed);

  void update_parsed_bytes(size_t parsed_bytes) {
    parsed_offset_ += parsed_bytes;
  }
  const char * unparsed_data() const {
    return data_ + parsed_offset_;
  }
  size_t unparsed_bytes() const;  // unparsed bytes

  size_t parsed_unprocessed_bytes() const {
    assert(parsed_offset_ >= processed_offset_);
    if (parsed_offset_ > processed_offset_) {
      return parsed_offset_ - processed_offset_;
    }
    return 0;
  }
  size_t parsed_unreceived_bytes() const {
    if (parsed_offset_ > received_offset_) {
      return parsed_offset_ - received_offset_;
    }
    return 0;
  }
  size_t unparsed_received_bytes() const {
    if (received_offset_ > parsed_offset_) {
      return received_offset_ - parsed_offset_;
    }
    return 0;
  }
private:
  void try_recycle_buffer();
};

}

#endif // _YARMPROXY_READ_BUFFER_H_

