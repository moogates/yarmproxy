#ifndef _YAMPROXY_ALLOCATOR_H_
#define _YAMPROXY_ALLOCATOR_H_

#include <set>

namespace yarmproxy {

class Allocator {
public:
  Allocator(int buffer_size, int reserved_size);
  char* Alloc();
  void Release(char*);
  int buffer_size() const {
    return buffer_size_;
  }
private:
  int buffer_size_;

  char* reserved_space_;
  int reserved_space_size_;

  std::set<char*> free_slabs_;
};

}

#endif // _YAMPROXY_ALLOCATOR_H_

