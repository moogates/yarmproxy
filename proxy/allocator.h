#ifndef _YAMPROXY_ALLOCATOR_H_
#define _YAMPROXY_ALLOCATOR_H_

#include <set>

namespace yarmproxy {

class Allocator {
public:
  Allocator(int slab_size, int slab_count);
  char* Alloc();
  void Release(char*);
  int slab_size() const {
    return slab_size_;
  }
private:
  int slab_size_;
  int slab_count_;

  std::set<char*> free_slabs_;
  char* chunk_;
};

}

#endif // _YAMPROXY_ALLOCATOR_H_

