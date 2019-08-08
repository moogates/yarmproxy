#include "FeedTestI.h"
#include "QueryRunner.h"
#include "ServiceI.h"
#include "FeedMemcProxy/client/feed_memc_client.h"
#include "FeedMemcProxy/client/user_profile_client.h"


using namespace xce::feed;
using namespace xce::ad;

using namespace com::xiaonei::xce;
using namespace MyUtil;

void MyUtil::initialize() {
  ServiceI& service = ServiceI::instance();
  service.getAdapter()->add(&MemcFeedTestI::instance(), service.createIdentity("M", ""));
}

FeedContentDict MemcFeedTestI::GetFeedDict(const MyUtil::LongSeq & ids, const Ice::Current&) {
  FeedContentDict dict = FeedMemcClient::instance().GetFeedDict(ids);
  MCE_INFO("FeedTestI::GetFeedDict --> ids:" << ids.size() << " res:" << dict.size());
  return dict;
}

FeedContentPtr MemcFeedTestI::GetFeed(Ice::Long id, const Ice::Current&) {
  // for UserProfileClient test only
  UserProfile profile;
  MCE_INFO("get profile : " << id );
  bool b = UserProfileClient::instance().Get(id, &profile);
  if (b) {
  MCE_INFO("profile fields : " 
      << " id " << profile.id()
      << " stage " << profile.stage()
      << " gender " << profile.gender()
      << " age " << profile.age()
      << " school " << profile.school()
      << " major " << profile.major()
      << " grade " << profile.grade()
      << " home_area " << profile.home_area() << "@" << profile.home_area().size()
      << " current_area " << profile.current_area() << "@" << profile.current_area().size()
      << " ip " << profile.ip()
      << " ip_area " << profile.ip_area() << "@" << profile.ip_area().size() );
  } else {
    MCE_INFO("profile not found. id=" << id); 
  }

  return FeedMemcClient::instance().GetFeed(id);
};

