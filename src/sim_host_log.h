#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstring>

#ifdef CROSSPOINT_SIM_GRPC
#include "sim_grpc/session_client.h"
#endif

// Severity values match Session LogSeverity so they pass through emitLog.
enum {
  SIM_HOST_DEBUG = 1,
  SIM_HOST_INFO = 2,
  SIM_HOST_WARN = 3,
  SIM_HOST_ERROR = 4,
};

// Host [SIM] diagnostics. Always print to stderr. After SimGrpc::start(),
// also tee as LogLine LOG_TYPE_HOST_SIM.
inline void simHostLog(int severity, const char *component, const char *fmt,
                       ...) {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  const int written = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (written < 0) {
    return;
  }
  fputs(buf, stderr);
  if (buf[0] == '\0' || buf[std::strlen(buf) - 1] != '\n') {
    fputc('\n', stderr);
  }
#ifdef CROSSPOINT_SIM_GRPC
  if (written > 0) {
    const size_t end = written < static_cast<int>(sizeof(buf))
                           ? static_cast<size_t>(written)
                           : sizeof(buf) - 1;
    if (end > 0 && buf[end - 1] == '\n') {
      buf[end - 1] = '\0';
    }
  }
  SimGrpc::emitLog(SimGrpc::kLogTypeHostSim, severity,
                   component ? component : "sim", buf);
#endif
}
