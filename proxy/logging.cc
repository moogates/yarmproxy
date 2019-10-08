#include "logging.h"

#include <cstring>

namespace logging {

static Verbosity VerbosityFromName(const char* level);

bool StreamLogger::Initialize(const char* file_name, const char* verbosity_name) {
  auto file = fopen(file_name, "w"); //w = truncate, a = append
  if (!file) {
    LOG_ERROR << "Failed to open log file '" << file_name << "'";
    return false;
  }
  LOG_INFO << "Logging to '" << file_name << "', level=" << verbosity_name;

  global_verbosity_ = VerbosityFromName(verbosity_name);
  {
    std::lock_guard<std::mutex> lock(write_mutex_);
    fflush(log_file_);
    log_file_ = file;
  }
  LOG_INFO << "Start logging to '" << file_name << "', level=" << verbosity_name;
  return true;
}

std::mutex StreamLogger::write_mutex_;
Verbosity StreamLogger::global_verbosity_ = Verbosity::DEBUG;
FILE* StreamLogger::log_file_ = stdout;

static const char* VerbosityName(Verbosity v) {
  switch(v) {
  case Verbosity::TRACE:
    return "TRACE";
  case Verbosity::DEBUG:
    return "DEBUG";
  case Verbosity::INFO:
    return "INFO";
  case Verbosity::WARN:
    return "WARN";
  case Verbosity::ERROR:
    return "ERROR";
  case Verbosity::FATAL:
    return "FATAL";
  case Verbosity::OFF:
    return "OFF";
  default:
    return "BAD";
  }
}

static void CurrentDateTime(char* buff, size_t buff_size) {
  using namespace std::chrono;
  auto now = system_clock::now();
  long long ms_since_epoch = duration_cast<milliseconds>(
                                now.time_since_epoch()).count();
  time_t sec_since_epoch = time_t(ms_since_epoch / 1000);
  tm time_info;
  localtime_r(&sec_since_epoch, &time_info);
  snprintf(buff, buff_size, "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
    1900 + time_info.tm_year, 1 + time_info.tm_mon, time_info.tm_mday,
    time_info.tm_hour, time_info.tm_min, time_info.tm_sec,
    ms_since_epoch % 1000);
}

static Verbosity VerbosityFromName(const char* level) {
  if (strcmp("TRACE", level) == 0) {
    return Verbosity::TRACE;
  }
  if (strcmp("DEBUG", level) == 0) {
    return Verbosity::DEBUG;
  }
  if (strcmp("INFO", level) == 0) {
    return Verbosity::INFO;
  }
  if (strcmp("WARN", level) == 0) {
    return Verbosity::WARN;
  }
  if (strcmp("ERROR", level) == 0) {
    return Verbosity::ERROR;
  }
  if (strcmp("FATAL", level) == 0) {
    return Verbosity::FATAL;
  }
  if (strcmp("OFF", level) == 0) {
    return Verbosity::OFF;
  }
  return Verbosity::INFO;
}

StreamLogger::~StreamLogger() noexcept(false) {
  // TODO : option 是否立即flush
  char date_time[64];
  CurrentDateTime(date_time, sizeof(date_time));

  {
    std::lock_guard<std::mutex> lock(write_mutex_);
    fprintf(log_file_, "%s [%5s] %s\n", date_time,
            VerbosityName(current_verbosity_), oss_.str().c_str());
  }
  fflush(log_file_); // TODO : flush 是否有必要在lock之内?
}

}

