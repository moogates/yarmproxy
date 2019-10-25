#include "allocator.h"

#include "logging.h"

namespace yarmproxy {

Allocator::Allocator(int buffer_size, int reserved_space_size)
    : buffer_size_(buffer_size)
    , reserved_space_size_(reserved_space_size) {
  LOG_WARN << "Allocator ctor, buffer_size=" << buffer_size
           << " reserved_space_size=" << reserved_space_size;
  if (reserved_space_size == 0) {
    reserved_space_ = nullptr;
  } else {
    reserved_space_ = new char[reserved_space_size]; // TODO : 内存位置对齐
    for(auto p = reserved_space_; p < reserved_space_ + reserved_space_size; p += buffer_size) {
      free_slabs_.insert(p);
    }
  }
}

char* Allocator::Alloc() {
  // return new char[buffer_size_];  // TODO : Why pre-allocated chunk slower than malloc()? locality?
  if (free_slabs_.empty()) {
    LOG_INFO << "Allocator::Alloc no free slab";
    return new char[buffer_size_];
  } else {
    char* slab = *(free_slabs_.begin());
    free_slabs_.erase(free_slabs_.begin());
    LOG_INFO << "Allocator::Alloc free_count=" << free_slabs_.size()
             << " allocated=" << (void*)slab;
    return slab;
  }
}

void Allocator::Release(char* slab) {
  if (reserved_space_ == nullptr || slab < reserved_space_ ||
      slab >= reserved_space_ + reserved_space_size_) {
    LOG_INFO << "Allocator::Release delete buffer";
    delete []slab;
  }
  free_slabs_.insert(slab);
  LOG_INFO << "Allocator::Release recycle " << slab
           << " free_count=" << free_slabs_.size();
}

}

