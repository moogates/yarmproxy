#ifndef _YARMPROXY_PROTOCOL_TYPE_H_
#define _YARMPROXY_PROTOCOL_TYPE_H_

namespace yarmproxy {

enum class ProtocolType {
  NONE      = 0,
  REDIS     = 1,
  MEMCACHED = 2,
};

}

#endif // _YARMPROXY_PROTOCOL_TYPE_H_
