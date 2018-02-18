#pragma once
#include "utils.h"
#include "debugbreak.h"

#ifdef __APPLE_API_UNSTABLE
bool AmIBeingDebugged();
#else
// TODO: find ways of detecting this on other platforms
#ifdef ZION_DEBUG
#define AmIBeingDebugged() true
#else
#define AmIBeingDebugged() false
#endif
#endif

/* DEBUG preprocessor directives */
void init_dbg();
extern int __dbg_level;

#define debug_level() __dbg_level

#define dbg(x) do { \
	fprintf(stderr, "---------------- BREAKPOINT ----------------\n"); \
	log_dump(); \
	std::string dbg_msg = clean_ansi_escapes_if_not_tty( \
		stderr, \
		string_format(C_LINE_REF "%s:%d:1" C_RESET ": " c_id("dbg()") " hit in " c_internal("%s") " : %s\n", __FILE__, __LINE__, __PRETTY_FUNCTION__, #x)); \
	::fprintf(stderr, "%s", dbg_msg.c_str()); \
   	/*::log_stack(log_warning); */ \
	debug_break(); \
} while (0)

#define dbg_when(x) if (x) { dbg(); } else { }

#define wat() panic("wat is this branch doing?")

#ifdef ZION_DEBUG
#define debug(x) ((x))
#define debug_else(x, y) ((x))
#define debug_above(level, x) do { if (debug_level() >= (level)) (x); } while (0)
#define debug_ex(x) debug_above(2, x)
#else
#define debug(x)
#define debug_else(x, y) ((y))
#define debug_ex(x) 
#define debug_above(level, x)
#endif

#include "colors.h"

struct depth_guard_t {
	int &depth;
	depth_guard_t(int &depth, int max_depth);
	~depth_guard_t();
};

