#include "user_profile_client.h"

#include <boost/lexical_cast.hpp>

#include "compress_wrap.h"
#include "LogWrapper.h"

using namespace xce::feed;

namespace xce {
namespace ad {

//static char * kEmptyProfile = "EMPTY_INFO";

void GetUserKey(Ice::Int uid, char * key, size_t len) {
  snprintf(key, len, "ADUP#%d", uid);  
  key[len - 1] = 0;
}

UserProfileClient::UserProfileClient() {
}

UserProfileClient::~UserProfileClient() {
}

void UserProfileClient::Serialize(const UserProfile& profile, string * serialized) {
  profile.SerializeToString(serialized);
}

void UserProfileClient::Deserialize(const string & serialized, UserProfile * profile) {
  profile->ParseFromString(serialized);
}

bool UserProfileClient::GetSerialized(Ice::Int uid, string * serialized) {
  char key[32];
  GetUserKey(uid, key, 32);

  uint32_t flag = 0;
  size_t len;
  memcached_return rc;

  pair<int, memcached_st *> memc_pair = GetMemc();
  memcached_st * memc = memc_pair.second;

  char * v = memcached_get(memc, key, strlen(key), &len, &flag, &rc);

  if (rc != MEMCACHED_SUCCESS) {
    ReturnMemc(false, memc_pair);
    return false;
  }
  serialized->assign(v, len);

  ReturnMemc(true, memc_pair);
  free(v);
  return true;
}

bool UserProfileClient::Get(Ice::Int uid, UserProfile * profile) {
  string serialized;
  if (!GetSerialized(uid, &serialized)) {
    return false;
  }
  profile->ParseFromString(serialized);
  return true;
}


bool UserProfileClient::Set(const UserProfile & o) {
  char key[32];
  GetUserKey(o.id(), key, 32);
   
  string value;
  o.SerializeToString(&value);

  return SetMemcached(key, value, 0);
}

}
}

