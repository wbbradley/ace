#include "dbg.h"
#include <iostream>
#include "location.h"
#include "user_error.h"
#include <sstream>
#include <signal.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/sysctl.h>


int __dbg_level = 0;

void init_dbg() {
	const_cast<int&>(__dbg_level) = atoi((getenv("DEBUG") != nullptr) ? getenv("DEBUG") : "0");
}

void _emit_assert(
		const char *filename,
	   	int line,
	   	const char *assertion,
	   	const char *function)
{
	location_t location(filename, line, 1);
	std::stringstream ss;
	ss << c_error("assert failed");
	ss << " --> " << C_ERROR << assertion << C_RESET << " in ";
	ss << C_INTERNAL << function << C_RESET;
	log_location(log_panic, location, "%s", ss.str().c_str());
	log_dump();
	// ::log_stack(log_warning);
    DEBUG_BREAK();
}

depth_guard_t::depth_guard_t(location_t location, int &depth, int max_depth) : depth(depth) {
	++depth;
	if (depth > max_depth) {
		throw user_error(location, "maximum recursion depth reached");
	}
}

depth_guard_t::~depth_guard_t() {
	--depth;
}

#ifdef __APPLE_API_UNSTABLE
bool AmIBeingDebugged(void)
    // Returns true if the current process is being debugged (either 
    // running under the debugger or has a debugger attached post facto).
{
    int                 junk;
    int                 mib[4];
    struct kinfo_proc   info;
    size_t              size;

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
    junk = sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0);
    assert(junk == 0);

    // We're being debugged if the P_TRACED flag is set.

    return ( (info.kp_proc.p_flag & P_TRACED) != 0 );
}
#endif
