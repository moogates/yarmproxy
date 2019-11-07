#include "logging.h"

#include <cstring>

namespace base {

static loguru::Verbosity LevelVerbosity(const char* level) {
  if (strcmp("TRACE", level) == 0) {
    return 4;
  }
  if (strcmp("DEBUG", level) == 0) {
    return 2;
  }
  if (strcmp("INFO", level) == 0) {
    return loguru::Verbosity_INFO;     // 0
  }
  if (strcmp("WARN", level) == 0) {
    return loguru::Verbosity_WARNING;  // -1
  }
  if (strcmp("ERROR", level) == 0) {
    return loguru::Verbosity_ERROR;    // -2
  }
  if (strcmp("FATAL", level) == 0) {
    return loguru::Verbosity_FATAL;    // -3
  }
  if (strcmp("OFF", level) == 0) {
    return loguru::Verbosity_OFF;      // -9
  }
  return loguru::Verbosity_INFO;       // 0
}

void InitLogging(const char *path, const char *loglevel) {
  loguru::Verbosity verbosity = LevelVerbosity(loglevel);
  if (verbosity <= 0) {
    loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
    // loguru::g_preamble_date    = true; // The date field
    // loguru::g_preamble_time    = true; // The time of the current day
    loguru::g_preamble_uptime  = false; // The time since init call
    loguru::g_preamble_thread  = false; // The logging thread
    loguru::g_preamble_file    = false; // The file from which the log originates from
    // loguru::g_preamble_verbose = true; // The verbosity field
    // loguru::g_preamble_pipe    = true; // The pipe symbol right before the message
  }
  loguru::g_stderr_verbosity = verbosity;

  int argc = 1;
  char* argv[] = {const_cast<char*>("loguru"), nullptr};
  loguru::init(argc, argv);
  // loguru::add_file(path, loguru::Append, verbosity);
  loguru::add_file(path, loguru::Truncate, verbosity);
}

}

