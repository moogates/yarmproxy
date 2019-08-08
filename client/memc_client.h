#ifndef _XCE_MEMC_CLIENT_H_
#define _XCE_MEMC_CLIENT_H_

#include <map>
#include <string>
#include <vector>

#include <IceUtil/Mutex.h>
#include <libmemcached/memcached.h>

#include "TaskManager.h"

using namespace MyUtil;

namespace xce {
namespace feed {

class TimeStat1{
        timeval _begin, _end;
public:
        TimeStat1(){
                reset();
        }
        void reset(){
                gettimeofday(&_begin, NULL);
        }
        float getTime(){
                gettimeofday(&_end, NULL);
                float timeuse=1000000*(_end.tv_sec-_begin.tv_sec)+_end.tv_usec
                                -_begin.tv_usec;
                timeuse/=1000;
                return timeuse;
        }
};


const int kErrorLimit = 200;

class ProxyConnPool {
public:
  ProxyConnPool(int index);
  ~ProxyConnPool();

  bool Reset();

  memcached_st * Pop();
  void Push(memcached_st * memc);

  void ClearErrorCount() {
    error_count_ = 0;
  }
  int IncErrorCount();
  void CheckState();
  int size();
private:
  int proxy_index_;

  std::string proxy_addr_;
  unsigned short proxy_port_;

  bool reseting;
  int error_count_;
  std::vector<memcached_st *> memc_pool_;
  IceUtil::Mutex mutex_;
};

class MemcClient {
public:
  MemcClient();
  virtual ~MemcClient();

  bool SetMemcached(const char * key, const std::string & value, int32_t flag);
  void CheckPoolMap();
  int size(int i);
protected:
  std::pair<int, memcached_st *> GetMemc();
  bool ReturnMemc(bool success, std::pair<int, memcached_st *> memc_pair);
  void PushMemc(std::pair<int, memcached_st *> memc_pair);

  std::vector<ProxyConnPool*> pool_map_;
  int next_proxy_;
};

class MemcClientCheckTimer : public Timer {
public:
  MemcClientCheckTimer(MemcClient * mc) : Timer(3 * 1000), memc_client_(mc) {}
  virtual void handle();
private:
  MemcClient * memc_client_;
};

}
}
#endif // _XCE_MEMC_CLIENT_H_

