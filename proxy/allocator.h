#ifndef _YAMPROXY_ALLOCATOR_H_
#define _YAMPROXY_ALLOCATOR_H_

#include <set>

namespace mcproxy {

class Allocator {
public:
  Allocator(size_t slab_size, size_t slab_count);
  char* Alloc();
  void Release(char*);
  size_t slab_size() const {
    return slab_size_;
  }
private:
  size_t slab_size_;
  size_t slab_count_;

  std::set<char*> free_slabs_;
  char* chunk_;
};

}

#endif // _YAMPROXY_ALLOCATOR_H_

