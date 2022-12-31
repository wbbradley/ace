#include "dbg.h"

#include <iostream>
#include <signal.h>
#include <sstream>
#include <stdbool.h>
#ifdef __APPLE_API_UNSTABLE
#include <sys/sysctl.h>
#endif
#include <sys/types.h>
#include <unistd.h>

#include "location.h"
#include "user_error.h"

int __dbg_level = 0;
int __depth_level_override = 0;

void init_dbg() {
  __dbg_level = atoi((getenv("DEBUG") != nullptr) ? getenv("DEBUG") : "0");
  __depth_level_override = atoi(
      (getenv("IGNORE_DEPTH") != nullptr) ? getenv("IGNORE_DEPTH") : "0");
}

void _emit_assert(const char *filename,
                  int line,
                  const char *assertion,
                  const char *function) {
  Location location(filename, line, 1);
  std::stringstream ss;
  log_location(log_panic, location, c_error("============="));
  ss << c_error("assert failed");
  ss << " --> " << C_ERROR << assertion << C_RESET << " in ";
  ss << C_INTERNAL << function << C_RESET;
  log_location(log_panic, location, "%s", ss.str().c_str());
  log_location(log_panic, location, c_error("============="));
  log_dump();
  // ::log_stack(log_warning);
  DEBUG_BREAK();
}

DepthGuard::DepthGuard(Location location, int &depth, int max_depth)
    : depth(depth) {
  ++depth;
  if (!__depth_level_override && (depth > max_depth)) {
    throw zion::user_error(location, "maximum recursion depth reached");
  }
}

DepthGuard::~DepthGuard() {
  --depth;
}

#ifdef __APPLE_API_UNSTABLE
bool AmIBeingDebugged(void)
// Returns true if the current process is being debugged (either
// running under the debugger or has a debugger attached post facto).
{
#ifdef ZION_DEBUG
  int junk;
#endif
  int mib[4];
  struct kinfo_proc info;
  size_t size;

  // Initialize the flags so that, if sysctl fails for some bizarre
  // reason, we get a predictable result.

  info.kp_proc.p_flag = 0;

  // Initialize mib, which tells sysctl the info we want, in this case
  // we're looking for information about a specific process ID.

  mib[0] = CTL_KERN;
  mib[1] = KERN_PROC;
  mib[2] = KERN_PROC_PID;
  mib[3] = getpid();

  // Call sysctl.

  size = sizeof(info);
#ifdef ZION_DEBUG
  junk =
#endif
      sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0);
  assert(junk == 0);

  // We're being debugged if the P_TRACED flag is set.

  return ((info.kp_proc.p_flag & P_TRACED) != 0);
}
#endif
