#ifndef _XCE_FEED_MEMC_LOADER_H_
#define _XCE_FEED_MEMC_LOADER_H_

#include "Singleton.h"
#include "RFeed.h"
#include "MemcProxy.h"

namespace xce {
namespace feed {

using namespace std;
using namespace MyUtil;

// 从memc 调用. 作为取得 content 的例子
class MemcFeedTestI : public MemcFeedTest, public Singleton<MemcFeedTestI> {
  virtual FeedContentPtr GetFeed(Ice::Long id, const Ice::Current& = Ice::Current());
  virtual FeedContentDict GetFeedDict(const MyUtil::LongSeq & ids, const Ice::Current& = Ice::Current());
};

}
}

#endif // _XCE_FEED_MEMC_LOADER_H_
