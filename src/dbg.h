#pragma once
#include <iostream>
#include <signal.h>

#include "location.h"
#include "utils.h"

#ifdef __APPLE_API_UNSTABLE
bool AmIBeingDebugged();
#else
// TODO: find ways of detecting this on other platforms
#ifdef CIDER_DEBUG
#define AmIBeingDebugged() true
#else
#define AmIBeingDebugged() false
#endif
#endif

#ifdef _MSC_VER
#ifdef _X86_
#define DEBUG_BREAK()                                                          \
  {                                                                            \
    __asm { int 3 }                                                            \
  }
#else
#define DEBUG_BREAK()                                                          \
  { __debugbreak(); }
#endif
#else
#if __clang__ && 0
#define DEBUG_BREAK()                                                          \
  do {                                                                         \
    __debugbreak();                                                            \
    __noop;                                                                    \
  } while (0)
#else
#define DEBUG_BREAK()                                                          \
  do {                                                                         \
    if (AmIBeingDebugged()) {                                                  \
      raise(SIGTRAP);                                                          \
    } else {                                                                   \
      fprintf(                                                                 \
          stderr,                                                              \
          "exiting due to dbg() statement while not under the debugger\n");    \
      exit(-1);                                                                \
    }                                                                          \
  } while (0)
#endif
#endif

/* DEBUG preprocessor directives */
void init_dbg();
extern int __dbg_level;

#define debug_level() __dbg_level

#define dbg(x)                                                                 \
  do {                                                                         \
    fprintf(stderr, "---------------- BREAKPOINT ----------------\n");         \
    log_dump();                                                                \
    std::string dbg_msg = clean_ansi_escapes_if_not_tty(                       \
        stderr,                                                                \
        string_format(C_LINE_REF "%s:%d:1" C_RESET ": " c_id(                  \
                          "dbg()") " hit in " c_internal("%s") " : %s\n",      \
                      __FILE__, __LINE__, __PRETTY_FUNCTION__, #x));           \
    ::fprintf(stderr, C_ERROR "==============================" C_RESET "\n");  \
    ::fprintf(stderr, "%s", dbg_msg.c_str());                                  \
    ::fprintf(stderr, C_ERROR "==============================" C_RESET "\n");  \
    /*::log_stack(log_warning); */                                             \
    DEBUG_BREAK();                                                             \
  } while (0)

#ifdef CIDER_DEBUG
#define dbg_when(x)                                                            \
  if (x) {                                                                     \
    fprintf(stderr,                                                            \
            C_ERROR "======    " C_RESET C_CONTROL                             \
                    "dbg_when(%s)" C_RESET C_ERROR "    ======" C_RESET "\n",  \
            #x);                                                               \
    dbg();                                                                     \
  } else {                                                                     \
  }

#define dbg_not(x) dbg_when(!(x))
#else
#define dbg_when(x)
#define dbg_not(x)
#endif

#define wat() panic("wat is this branch doing?")

#ifdef CIDER_DEBUG
#define debug(x) ((x))
#define debug_else(x, y) ((x))
#define debug_above(level, x)                                                  \
  do {                                                                         \
    if (debug_level() >= (level))                                              \
      (x);                                                                     \
  } while (0)
#define debug_above_else(level, x, y) ((debug_level() >= (level)) ? (x) : (y))
#define debug_ex(x) debug_above(2, x)
#else
#define debug(x)
#define debug_else(x, y) ((y))
#define debug_ex(x)
#define debug_above(level, x)
#endif

#include "colors.h"

struct DepthGuard {
  int &depth;
  DepthGuard(Location location, int &depth, int max_depth);
  ~DepthGuard();
};
