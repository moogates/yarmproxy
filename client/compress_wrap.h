#ifndef _XCE_FEED_COMPRESS_WRAP_H_
#define _XCE_FEED_COMPRESS_WRAP_H_

#include "Singleton.h"

namespace xce {
namespace feed {

using namespace std;
using namespace MyUtil;

#define QLZ_COMPRESSION_LEVEL 1
#define QLZ_STREAMING_BUFFER  1000000 

class CompressWrap : public Singleton<CompressWrap> {
public:
  bool Compress(const string& data, string * output);

  // 解压的时候, 不需要指定数据的长度
  void Decompress(const char * data, string * output);
  void Decompress(const string& data, string * output);
};

}
}

#endif // _XCE_FEED_COMPRESS_WRAP_H_
