#include "dbg.h"
#include <iostream>

void _emit_assert(
		const char *filename,
	   	int line,
	   	const char *assertion,
	   	const char *function)
{
	std::cerr << C_FILENAME << filename << "(" << line << ")" << C_RESET ": ";
	std::cerr << c_error("assert failed") << std::endl;
	std::cerr << "--> " << C_ERROR << assertion << C_RESET << " in ";
	std::cerr << C_INTERNAL << function << C_RESET << std::endl;
	/* ::log_stack(log_warning); */ \
	__debugbreak();
	__noop;
}

depth_guard_t::depth_guard_t(int &depth, int max_depth) : depth(depth) {
	++depth;
	if (depth > max_depth) {
		std::cerr << c_error("maximum depth reached") << std::endl;
		__debugbreak();
		__noop;
	}
}

depth_guard_t::~depth_guard_t() {
	--depth;
}
