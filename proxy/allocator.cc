#include "allocator.h"

#include "base/logging.h"

namespace yarmproxy {

Allocator::Allocator(int slab_size, int slab_count)
    : slab_size_(slab_size)
    , slab_count_(slab_count) {
  chunk_ = new char[slab_size * slab_count]; // TODO : 内存位置对齐fix
  for(int i = 0; i < slab_count; ++i) {
    free_slabs_.insert(chunk_ + i * slab_size);
  }
}

char* Allocator::Alloc() {
  return new char[slab_size_];  // TODO : Why pre-allocated chunk slower than malloc()? locality?
  if (free_slabs_.empty()) {
    LOG_INFO << "Allocator::Alloc no free slab";
    return new char[slab_size_];
  } else {
    char* slab = *(free_slabs_.begin());
    free_slabs_.erase(free_slabs_.begin());
    LOG_INFO << "Allocator::Alloc has free slab, size=" << free_slabs_.size()
             << " addr=" << (void*)slab;
    return slab;
  }
}

void Allocator::Release(char* slab) {
  delete []slab;
  return;
  free_slabs_.insert(slab);  // recycle all
  return;

  if (slab < chunk_ || slab >= chunk_ + slab_size_ * slab_count_) {
    LOG_INFO << "Allocator::Alloc delete a slab";
    delete []slab;
  }
  LOG_INFO << "Allocator::Alloc recycle a slab, size=" << free_slabs_.size();
  free_slabs_.insert(slab);
}

}

