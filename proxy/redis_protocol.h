#ifndef _YARMPROXY_REDIS_PROTOCOL_H_
#define _YARMPROXY_REDIS_PROTOCOL_H_

namespace yarmproxy {
namespace redis {

int ParseArrayOfBulk(const char* data, size_t bytes) {
  // [] -> "*0\r\n"
  // ["foo", "bar"] -> "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n"
  // [1, 2, 3] -> "*3\r\n:1\r\n:2\r\n:3\r\n"
  // [1, 2, "hello"]  ->  "*3\r\n:1\r\n:2\r\n+hello\r\n" (array with mixed types)
  return 0;
}

int ParseInteger(const char* data, size_t bytes) {
  // 1000 -> ":1000\r\n"
  return 0;
}

int ParseErrorString(const char* data, size_t bytes) {
  // "SERVER_ERROR" -> "-SERVER_ERROR\r\n"
  return 0;
}

int ParseSimpleString(const char* data, size_t bytes) {
  // "HELLO" -> "+HELLO\r\n"
  return 0;
}

int ParseBulkString(const char* data, size_t bytes) {
  // "result" -> "$6\r\nresult\r\n"
  // ""  -> "$0\r\n\r\n"
  // nil  -> "$-1\r\n" (null bulk string)
  return 0;
}

}
}

#endif // _YARMPROXY_REDIS_PROTOCOL_H_

