
#include "feed_memc_client.h"

#include <boost/lexical_cast.hpp>

#include "compress_wrap.h"
#include "FeedContent.pb.h"

#include "ServiceI.h"
#include "LogWrapper.h"

namespace xce {
namespace feed {

// libmemcached mget() bug : kEmptyFeed 不可以是空串
static char * kEmptyFeed = "EMPTY_FEED";
Ice::Long g_key = 0;
IceUtil::Mutex g_mutex;
void GetFeedKey(Ice::Long fid, char * key, size_t len) {
  snprintf(key, len, "FEED#%ld", fid);  
  key[len - 1] = 0;
}

Ice::Long GetFeedId(char * key, size_t len) {
  if (len < 5 || strncmp("FEED#", key, 5) != 0) {
    return -1;
  }

  Ice::Long id = -1;
  try {
    id = boost::lexical_cast<Ice::Long>(string(key + 5, len - 5));
  } catch(boost::bad_lexical_cast &) {
    return -1;
  }
  return id;
}

static void FeedContentPbToIce(const PbFeedContent & pb, FeedContentPtr content) {
  if (!content->data) {
    content->data = new FeedData();
  }

  content->data->feed = pb.data().feed();
  content->data->source = pb.data().source();
  content->data->actor = pb.data().actor();
  content->data->type = pb.data().type();
  content->data->time = pb.data().time();
  content->data->weight = pb.data().weight();
  content->data->xml = pb.data().xml();

  if (pb.has_reply()) {
    content->reply = new FeedReply();

    content->reply->oldReplyId = pb.reply().oldreplyid();
    content->reply->oldReply = pb.reply().oldreply();
    content->reply->newReplyId = pb.reply().newreplyid();
    content->reply->newReply = pb.reply().newreply();
    content->reply->count = pb.reply().count();
  }
}

FeedMemcClient::FeedMemcClient() {
}

FeedMemcClient::~FeedMemcClient() {
}

FeedContentDict FeedMemcClient::GetFeedDict(const MyUtil::LongSeq& ids) {
  char ** keys = new char*[ids.size()];
  char * buffer = new char[ids.size() * 32];
  size_t * keys_length = new size_t[ids.size()];
  
  TimeStat1 totalts;
  float parsertime = 0;
  float fetchtime = 0;
  float gettime = 0;
  float getmemctime = 0;
  FeedContentDict dict;
  try{
  for(size_t i = 0; i < ids.size(); ++i)
  {
    keys[i] = buffer + i * 32;
    GetFeedKey(ids[i], keys[i], 32);
    keys_length[i] = strlen(keys[i]);
  }


  //try 3 times
  const int RETRY_TIMES = 3;
  pair<int, memcached_st *> memc_pair = GetMemc();
  memcached_st * memc = NULL;
  memcached_return rc;
  for(int i=0; i<RETRY_TIMES; i++){
    if(memc != NULL){
      ReturnMemc(false, memc_pair);
    }
    TimeStat1 ts2;
    memc_pair = GetMemc();
    getmemctime = ts2.getTime();
    memc = memc_pair.second;
    TimeStat1 ts;
    rc = memcached_mget(memc, keys, keys_length, ids.size());
    gettime = ts.getTime();
    
    //MCE_INFO("FeedMemcClient::GetFeedDict --> retrun from proxy ids:" << ids.size() 
    //      << " cost:" << ts.getTime() << " getmemc:" << ec
    //      << " index:" << memc_pair.first << " p:" << memc << " try:" << i 
    //      << " err, " << memcached_strerror(memc, rc) << " sys err, " << errno << " " << strerror(errno));
    if(rc == MEMCACHED_SUCCESS){
      break;
    }
  }

  char return_key[MEMCACHED_MAX_KEY];
  size_t return_key_length;
  char * return_value;
  size_t return_value_length;
  uint32_t flags;


  bool error_flag = false;
  
  TimeStat1 ts;
  if (rc == MEMCACHED_SUCCESS) {
	ts.reset();
    while ((return_value = memcached_fetch(memc, return_key, &return_key_length,
                                          &return_value_length, &flags, &rc)) != NULL) {
      fetchtime += ts.getTime();
      if (rc == MEMCACHED_SUCCESS) {
        if ((return_value_length == strlen(kEmptyFeed)) 
            && strncmp(return_value, kEmptyFeed, return_value_length) == 0) {
          Ice::Long id = GetFeedId(return_key, return_key_length);
          if (id > 0) {
            dict.insert(make_pair(id, FeedContentPtr(NULL)));
          }
          free(return_value);
          continue;
        }
        
        PbFeedContent pb;
        if (flags & 0x01) {
          string decompressed;
          TimeStat1 ts1;
          CompressWrap::instance().Decompress(return_value, &decompressed);
          parsertime += ts1.getTime();
          pb.ParseFromString(decompressed);
        } else {
          pb.ParseFromString(return_value);
        }
        FeedContentPtr content = new FeedContent();
        FeedContentPbToIce(pb, content);

        dict.insert(make_pair(content->data->feed, content));
        
        MCE_INFO("FeedMemcClient::GetFeedDict --> paser data in while");
        free(return_value);
        
      } else {
        //MCE_INFO("FeedMemcClient::GetFeedDict --> paser ids:" << ids.size() << " cost:" << ts.getTime()
        //    << " err, " << memcached_strerror(memc, rc));
        error_flag = true;
        free(return_value);
        break;
      }
      ts.reset();
    }
  } else {
    error_flag = true;
  }

  ReturnMemc(!error_flag, memc_pair);
  }catch(std::exception& e){
	MCE_WARN("FeedMemcClient::GetFeedDict --> err, " << e.what());
  }catch(...){
	MCE_WARN("FeedMemcClient::GetFeedDict --> err");
  }
//if
//if (error_flag) {
//  memcached_free(memc);
//  ++ error_count_;
//} else {
//  PushMemc(memc);
//  error_count_ = 0;
//}

//if (error_count_ > kErrorLimit) {
//  MCE_WARN("GetFeedDict error : reset memc proxy pool");
//  ResetMemcPool();
//}

  delete [] keys;
  delete [] buffer;
  delete [] keys_length;
  MCE_INFO("FeedMemcClient::GetFeedDict --> ids:" << ids.size() << " res:" << dict.size() << "parser:" << parsertime << " fetch:" << fetchtime 
          << " get:" << gettime << " getmemc:" << getmemctime << " total:" << totalts.getTime()
          );
  return dict;
}

FeedContentPtr FeedMemcClient::GetFeed(Ice::Long feed_id) {
  char key[32];
  GetFeedKey(feed_id, key, 32);

  uint32_t flag = 0;
  size_t len;

  memcached_return rc;

  pair<int, memcached_st *> memc_pair = GetMemc();
  memcached_st * memc = memc_pair.second;

  // TODO : 这里的内存, 应该由谁来释放?
  char * v = memcached_get(memc, key, strlen(key), &len, &flag, &rc);

  if (rc != MEMCACHED_SUCCESS) {
    ReturnMemc(false, memc_pair);
    return NULL;
  }
  
  if ((len == strlen(kEmptyFeed)) && strncmp(v, kEmptyFeed,len) == 0) {
    ReturnMemc(true, memc_pair);
    free(v);
    return NULL;
  }

  PbFeedContent pb;
  if (flag & 0x01) {
    string decompressed;
    CompressWrap::instance().Decompress(v, &decompressed);
    pb.ParseFromString(decompressed);
  } else {
    pb.ParseFromString(v);
  }

  ReturnMemc(true, memc_pair);
  free(v);

  FeedContentPtr content = new FeedContent();
  FeedContentPbToIce(pb, content);
  return content;
}

// 返回值, 标识是否压缩了
bool FeedMemcClient::SerializeFeed(const FeedContentPtr& content, string * serialized) {
  if (!content) {
    return false;
  }
  PbFeedContent pb;
  // TODO : 哪些字段是optional的? 可以优化
  pb.mutable_data()->set_feed(content->data->feed);
  pb.mutable_data()->set_source(content->data->source);
  pb.mutable_data()->set_actor(content->data->actor);
  pb.mutable_data()->set_type(content->data->type);
  pb.mutable_data()->set_time(content->data->time);
  pb.mutable_data()->set_weight(content->data->weight);
  pb.mutable_data()->set_xml(content->data->xml);

  if (content->reply) {
    pb.mutable_reply()->set_oldreplyid(content->reply->oldReplyId);
    pb.mutable_reply()->set_newreplyid(content->reply->newReplyId);
    pb.mutable_reply()->set_count(content->reply->count);
    pb.mutable_reply()->set_oldreply(content->reply->oldReply);
    pb.mutable_reply()->set_newreply(content->reply->newReply);
  }

  pb.SerializeToString(serialized);

  std::string compressed;
  bool b = CompressWrap::instance().Compress(*serialized, &compressed);
  if (b) {
    serialized->swap(compressed);
  }
  return b;
}

bool FeedMemcClient::SetEmptyFeed(Ice::Long id) {
  char key[32];
  GetFeedKey(id, key, 32);
  return SetMemcached(key, kEmptyFeed, 0);
}

bool FeedMemcClient::SetFeed(const FeedContentPtr& content) {
  char key[32];
  GetFeedKey(content->data->feed, key, 32);

  string value;
  bool b = SerializeFeed(content, &value);

  return SetMemcached(key, value, b ? 0x01 : 0);
}

}
}

