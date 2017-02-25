#pragma once
#include "logger_decls.h"
#include <csignal>

void _emit_assert(
		const char *filename,
		int line,
		const char *assertion,
		const char *function);

#ifdef assert
#undef assert
#endif

#define verbose() (getenv("DEBUG") != nullptr)

#ifdef ZION_DEBUG
#define assert(x) do { if (!(x)) { _emit_assert(__FILE__, __LINE__, #x, __PRETTY_FUNCTION__); } } while (0)
#define null_impl() (_emit_assert(__FILE__, __LINE__, "null impl", __PRETTY_FUNCTION__), nullptr)
#else
#define assert(x) ((void)0)
#define null_impl()
#endif // ZION_DEBUG

#define not_impl() assert(!"not yet implemented")

#ifndef assert
#error We should have had assert defined in here.
#endif

#define ship_assert(x) do { int y = (x); if (!y) assert(!#x); } while (0)

#ifdef ZION_DEBUG
#define assert_implies(x, y) do { if (x) assert(y); } while (0)
#else
#define assert_implies(x, y)
#endif

#define panic(msg) panic_(__FILE__, __LINE__, msg)

