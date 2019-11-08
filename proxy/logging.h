#pragma once

#define LOGURU_WITH_STREAMS 1
#include "loguru.h"

namespace base {

void InitLogging(const char *path, const char *loglevel);
#define LOG_INIT(path, loglevel) \
  base::InitLogging(path, loglevel);

#define LOG_TRACE LOG_S(4)
#define LOG_DEBUG LOG_S(2)
#define LOG_INFO  LOG_S(INFO)
#define LOG_WARN  LOG_S(WARNING)
#define LOG_ERROR LOG_S(ERROR)
#define LOG_FATAL LOG_S(FATAL)

}

