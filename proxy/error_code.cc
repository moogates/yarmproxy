#include "error_code.h"
namespace yarmproxy {

const char* ErrorCodeMessage(ErrorCode ec) {
  switch(ec) {
  case ErrorCode::E_SUCCESS:
    return "E_SUCCESS";
  case ErrorCode::E_CONNECT:
    return "E_CONNECT";
  case ErrorCode::E_READ_QUERY:
    return "E_READ_QUERY";
  case ErrorCode::E_WRITE_QUERY:
    return "E_WRITE_QUERY";
  case ErrorCode::E_READ_REPLY:
    return "E_READ_REPLY";
  case ErrorCode::E_WRITE_REPLY:
    return "E_WRITE_REPLY";
  case ErrorCode::E_PROTOCOL:
    return "E_PROTOCOL";
//case ErrorCode::E_TIMEOUT:
//  return "E_TIMEOUT";

  case ErrorCode::E_BACKEND_CONNECT_TIMEOUT:
    return "E_BACKEND_CONNECT_TIMEOUT";
  case ErrorCode::E_BACKEND_WRITE_TIMEOUT:
    return "E_BACKEND_WRITE_TIMEOUT";
  case ErrorCode::E_BACKEND_READ_TIMEOUT:
    return "E_BACKEND_READ_TIMEOUT";

  default:
    return "E_UNKNOWN";
  }
}

}

