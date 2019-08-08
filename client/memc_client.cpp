#include "memc_client.h"

#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/xtime.hpp>
#include <google/protobuf/stubs/common.h>

#include "ServiceI.h"
#include "LogWrapper.h"

#include "MemcProxyAdapter.h"

namespace xce {
namespace feed {

static const int kProxyClusterSize = 10;
static const int kMaxErrorCount = 50;
static const size_t kMaxPoolSize = 64;

ProxyConnPool::ProxyConnPool(int index) : proxy_index_(index)
    , proxy_port_(0)
    , reseting(false)
    , error_count_(0) {
}

ProxyConnPool::~ProxyConnPool() {
  IceUtil::Mutex::Lock lock(mutex_);
  for(size_t i = 0; i < memc_pool_.size(); ++ i) {
    memcached_free(memc_pool_[i]);
  }
}

bool ProxyConnPool::Reset() {
  MCE_WARN("ProxyConnPool Reset(). index=" << proxy_index_);
  {
    IceUtil::Mutex::Lock lock(mutex_);
    if (reseting) {
      return true;
    }
    reseting = true;

    while (!memc_pool_.empty()) {
      memcached_free(memc_pool_.back());
      memc_pool_.pop_back();
    }
  }

  string endpoint;
  try {
    endpoint = MemcProxyAdapter::instance().GetEndpoint(proxy_index_);
  } catch(Ice::Exception& e) {
    MCE_WARN("MemcProxyAdapter::GetEndpoint Ice error : " << e);
  } catch (...) {
    MCE_WARN("MemcProxyAdapter::GetEndpoint error.");
  }

  size_t pos = endpoint.find(':');
  if (pos != string::npos) {
    proxy_addr_ = endpoint.substr(0, pos);
    proxy_port_ = boost::lexical_cast<unsigned short>(endpoint.c_str() + pos + 1);
  }

  MCE_INFO("MemcProxy " << proxy_index_ << " endpoint : " << endpoint);
  if (proxy_addr_.empty() || proxy_port_ <= 0) {
    reseting = false;
    return false;
  }

  memcached_return rc;
  memcached_server_st * servers = NULL;
  servers = memcached_server_list_append(servers, proxy_addr_.c_str(), proxy_port_, &rc);
  memcached_st * memc = memcached_create(NULL);
  rc = memcached_server_push(memc, servers); // 向服务集群添加server list
  if (rc != MEMCACHED_SUCCESS) {
    reseting = false;
    return false;
  }

  {
    IceUtil::Mutex::Lock lock(mutex_);
    memc_pool_.push_back(memc);
    reseting = false;
  }
  error_count_ = 0;

  return true;
}

memcached_st * ProxyConnPool::Pop() {
  if (error_count_ > kMaxErrorCount) {
    return NULL;
  }

  memcached_st * memc = 0;
  IceUtil::Mutex::Lock lock(mutex_);
  if (!reseting && !memc_pool_.empty()) {
    if (memc_pool_.size() > 1) {
      memc = memc_pool_.back();
      memc_pool_.pop_back();
    } else {
      memc = memcached_clone(memc, memc_pool_.front());
    }
  }

  return memc;
}

void ProxyConnPool::CheckState() {
  if (error_count_ > kMaxErrorCount) {
    MCE_WARN("ProxyConnPool " << proxy_index_ << " reseted");
    Reset();
  }
}

int ProxyConnPool::IncErrorCount() {
  return ++ error_count_;
}

void ProxyConnPool::Push(memcached_st * memc) {
  IceUtil::Mutex::Lock lock(mutex_);
  if (!reseting && memc_pool_.size() < kMaxPoolSize) {
    memc_pool_.push_back(memc);
  } else {
    memcached_free(memc);
  }
}

int ProxyConnPool::size(){
  IceUtil::Mutex::Lock lock(mutex_);
  return memc_pool_.size();
}
/////////////////////////////////////////

MemcClient::MemcClient() : next_proxy_(0) {
  // TODO : use boost run_once()
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  for(int i = 0; i < kProxyClusterSize; ++i) {
    pool_map_.push_back(new ProxyConnPool(i));
    MCE_INFO("init conn pool. index=" << i);
    pool_map_.back()->Reset();
  }

  TaskManager::instance().schedule(new MemcClientCheckTimer(this));
}

MemcClient::~MemcClient() {
  for(size_t i = 0; i < pool_map_.size(); ++ i) {
    delete pool_map_[i];
    pool_map_[i] = NULL;
  }
}

void MemcClient::CheckPoolMap() {
  for(size_t i = 0; i < pool_map_.size(); ++ i) {
    pool_map_[i]->CheckState();
  }
}

int MemcClient::size(int i){
  if(i<pool_map_.size()){
    return pool_map_[i]->size();
  }else{
    return 0;
  }
}

pair<int, memcached_st *> MemcClient::GetMemc() {
  int index = 0;
  memcached_st * memc = 0;

  for(int i = 0; i < kProxyClusterSize; ++i) {
    ++ next_proxy_; // 不加锁. 可以容忍不准确
    next_proxy_%= kProxyClusterSize;
    index = next_proxy_;

    memc = pool_map_[index]->Pop();
    if (memc) {
      break;
    }
    MCE_WARN("GetMemc from pool " << index << " fail");
    pool_map_[index]->IncErrorCount();
  }

  return make_pair(index, memc);
}

void MemcClient::PushMemc(pair<int, memcached_st *> memc_pair) {
  pool_map_[memc_pair.first]->Push(memc_pair.second);
}

bool MemcClient::ReturnMemc(bool success, pair<int, memcached_st *> memc_pair) {
  ProxyConnPool * pool = pool_map_[memc_pair.first];

  if (!success) {
    memcached_free(memc_pair.second);
    pool->IncErrorCount();
    return false;
  }

  pool->Push(memc_pair.second);
  pool->ClearErrorCount();

  return true;
}

bool MemcClient::SetMemcached(const char * key, const string & value, int32_t flag) {
  pair<int, memcached_st *> memc_pair = GetMemc();
  memcached_st * memc = memc_pair.second;
  memcached_return rc = memcached_set(memc, key, strlen(key), value.data(), 
      value.size(), (time_t)0, flag);

  return ReturnMemc(rc == MEMCACHED_SUCCESS, memc_pair);
}
void MemcClientCheckTimer::handle() {
  memc_client_->CheckPoolMap();
  TaskManager::instance().schedule(new MemcClientCheckTimer(memc_client_));
}

}
}

