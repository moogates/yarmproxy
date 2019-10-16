#ifndef _LOGGING_H_
#define _LOGGING_H_

#include <cstdio>

#include <chrono>
#include <string>
#include <sstream>
#include <mutex>

namespace logging {

// usage:  LOG_DEBUG << "Foo " << std::setprecision(10) << some_value;
#define LOG(verbosity)                           \
  ((verbosity) > logging::StreamLogger::global_verbosity())   \
    ? (void)0                                    \
    : logging::Voidify() & logging::StreamLogger(verbosity)

#define LOG_INIT(file_name, verbosity_name) \
  logging::StreamLogger::Initialize(file_name, verbosity_name)

#define LOG_TRACE LOG(logging::Verbosity::TRACE)
#define LOG_DEBUG LOG(logging::Verbosity::DEBUG)
#define LOG_INFO  LOG(logging::Verbosity::INFO)
#define LOG_WARN  LOG(logging::Verbosity::WARN)
#define LOG_ERROR LOG(logging::Verbosity::ERROR)
#define LOG_FATAL LOG(logging::Verbosity::FATAL)

enum class Verbosity {
  TRACE  = 6,
  DEBUG  = 5,
  INFO   = 4,
  WARN   = 3,
  ERROR  = 2,
  FATAL  = 1,
  OFF    = 0,
};

bool InitLogging(const char *path, const char *loglevel);

class StreamLogger {
public:
  static bool Initialize(const char* file_name, const char* verbosity_name);
  static Verbosity global_verbosity() {
    return global_verbosity_;
  }

  StreamLogger(Verbosity verbosity) : current_verbosity_(verbosity){}
  ~StreamLogger() noexcept(false); // TODO : what's noexcept?

  template<typename T>
  StreamLogger& operator<<(const T& t) {
    oss_ << t;
    return *this;
  }

  // std::endl and other iomanip:s.
  StreamLogger& operator<<(std::ostream&(*f)(std::ostream&))
  {
    f(oss_);
    return *this;
  }

private:
  static std::mutex write_mutex_;
  static FILE* log_file_;
  static Verbosity global_verbosity_;

  Verbosity current_verbosity_;
  std::ostringstream oss_;
};

class Voidify
{
public:
  Voidify() {}
  // This has to be an operator with a precedence lower than << but higher than ?:
  void operator&(const StreamLogger&) {}
};

}

#endif //_LOGGING_H_

