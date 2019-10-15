#ifndef _YARMPROXY_ERROR_CODE_H_
#define _YARMPROXY_ERROR_CODE_H_

namespace yarmproxy {

enum class ErrorCode {
  E_SUCCESS  = 0,
  E_CONNECT  = 1,

  E_READ_QUERY  = 2,
  E_WRITE_QUERY = 3,

  E_READ_REPLY  = 4,
  E_WRITE_REPLY = 5,

  E_PROTOCOL = 6,
  E_WRITE_ABORTED = 7,

  E_BACKEND_CONNECT_TIMEOUT = 8,
  E_BACKEND_WRITE_TIMEOUT   = 9,
  E_BACKEND_READ_TIMEOUT    = 10,

  E_OTHERS   = 100,
};

const char* ErrorCodeString(ErrorCode ec);

}

#endif // _YARMPROXY_ERROR_CODE_H_

