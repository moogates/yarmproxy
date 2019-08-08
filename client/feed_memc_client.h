
#ifndef _XCE_FEED_MEMC_CLIENT_H_
#define _XCE_FEED_MEMC_CLIENT_H_

#include "RFeed.h"
#include "Singleton.h"
#include "memc_client.h"

namespace xce {
namespace feed {

using namespace std;
using namespace MyUtil;

class FeedMemcClient : public xce::feed::MemcClient, public Singleton<FeedMemcClient> {
public:
  ~FeedMemcClient();
  FeedMemcClient();

  FeedContentPtr GetFeed(Ice::Long id);
  FeedContentDict GetFeedDict(const MyUtil::LongSeq& ids);
  bool SetFeed(const FeedContentPtr& content);
  bool SetEmptyFeed(Ice::Long id);

  // 串行化的结果
  bool SerializeFeed(const FeedContentPtr& content, string * serialized);
protected:
};

}
}
#endif //_XCE_FEED_MEMC_CLIENT_H_
