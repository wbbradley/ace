#pragma once

#ifdef _MSC_VER
	#ifdef _X86_
		#define DEBUG_BREAK() { __asm { int 3 } }
	#else
		#define DEBUG_BREAK()  { __debugbreak(); }
	#endif
#else
	#if __clang__ && 0
		#define DEBUG_BREAK() do { __debugbreak(); __noop; } while (0)
	#else
		#define DEBUG_BREAK() do { raise(SIGTRAP); } while (0)
	#endif
#endif

/* DEBUG preprocessor directives */
void init_dbg();
extern int __dbg_level;

#define debug_level() __dbg_level

#define dbg(x) do { log_dump(); ::fprintf(stderr, C_LINE_REF "%s(%d) " C_RESET ": " c_warn("BREAKPOINT HIT") " in " c_internal("%s") " : %s\n", __FILE__, __LINE__, __PRETTY_FUNCTION__, #x); /*::log_stack(log_warning); */ DEBUG_BREAK(); } while (0)
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

