#pragma once

/* DEBUG preprocessor directives */
#define debug_level() (atoi((getenv("DEBUG") != nullptr) ? getenv("DEBUG") : "0"))

#define dbg(x) do { ::fprintf(stderr, C_LINE_REF "%s(%d) " C_RESET ": " c_warn("BREAKPOINT HIT") " in " c_internal("%s") " : %s\n", __FILE__, __LINE__, __PRETTY_FUNCTION__, #x); /*::log_stack(log_warning); */ __debugbreak(); __noop; } while (0)

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
