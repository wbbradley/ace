#include "dbg.h"
#include <iostream>
#include "location.h"
#include "status.h"
#include <sstream>
#include <signal.h>

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
	status_t status;
	std::stringstream ss;
	ss << c_error("assert failed");
	ss << " --> " << C_ERROR << assertion << C_RESET << " in ";
	ss << C_INTERNAL << function << C_RESET;
	user_message(log_panic, status, location, ss.str().c_str());
	log_dump();
	// ::log_stack(log_warning);
    DEBUG_BREAK();
}

depth_guard_t::depth_guard_t(int &depth, int max_depth) : depth(depth) {
	++depth;
	if (depth > max_depth) {
		std::cerr << c_error("maximum depth reached") << std::endl;
        DEBUG_BREAK();
	}
}

depth_guard_t::~depth_guard_t() {
	--depth;
}
