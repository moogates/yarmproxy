#include "allocator.h"

#include "base/logging.h"

namespace mcproxy {

Allocator::Allocator(size_t slab_size, size_t slab_count)
    : slab_size_(slab_size)
    , slab_count_(slab_count) {
  chunk_ = new char[slab_size * slab_count]; // TODO : 内存位置对齐fix
  for(size_t i = 0; i < slab_count; ++i) {
    free_slabs_.insert(chunk_ + i * slab_size);
  }
}

char* Allocator::Alloc() {
  if (free_slabs_.empty()) {
    LOG_INFO << "Allocator::Alloc no free slab";
    return new char[slab_size_];
  } else {
    char* slab = *(free_slabs_.begin());
    free_slabs_.erase(free_slabs_.begin());
    LOG_INFO << "Allocator::Alloc has free slab, size=" << free_slabs_.size();
    return slab;
  }
}

void Allocator::Release(char* slab) {
  if (slab < chunk_ || slab >= chunk_ + slab_size_ * slab_count_) {
    LOG_INFO << "Allocator::Alloc delete a slab";
    delete []slab;
  }
  LOG_INFO << "Allocator::Alloc recycle a slab, size=" << free_slabs_.size();
  free_slabs_.insert(slab);
}

}

