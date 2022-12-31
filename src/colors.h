#pragma once

/* ANSI color escape codes */
#ifndef NO_COLORED_OUTPUT
#define _ANSI_COLOR_RED "\x1b[31m"
#define _ANSI_COLOR_GREEN "\x1b[32m"
#define _ANSI_COLOR_YELLOW "\x1b[33m"
#define _ANSI_COLOR_BLUE "\x1b[34m"
#define _ANSI_COLOR_MAGENTA "\x1b[35m"
#define _ANSI_COLOR_CYAN "\x1b[36m"
#define _ANSI_COLOR_BRIGHT_WHITE "\x1b[37m"
#define _ANSI_COLOR_RESET "\x1b[0m"
#define _ANSI_COLOR_DIM "\x1b[1;37;30m"
#define _ANSI_COLOR_BRIGHT_GREEN "\x1b[1;32m"
#define _ANSI_COLOR_ORANGE "\x1b[38;2;243;134;48m"
#else
#define _ANSI_COLOR_RED ""
#define _ANSI_COLOR_GREEN ""
#define _ANSI_COLOR_YELLOW ""
#define _ANSI_COLOR_BLUE ""
#define _ANSI_COLOR_MAGENTA ""
#define _ANSI_COLOR_CYAN ""
#define _ANSI_COLOR_BRIGHT_WHITE ""
#define _ANSI_COLOR_RESET ""
#define _ANSI_COLOR_DIM ""
#define _ANSI_COLOR_BRIGHT_GREEN ""
#define _ANSI_COLOR_ORANGE ""
#endif

#define _COLOR(c, x) c x _ANSI_COLOR_RESET

#define _bright_white(x) _COLOR(_ANSI_COLOR_BRIGHT_WHITE, x)
#define _red(x) _COLOR(_ANSI_COLOR_RED, x)
#define _green(x) _COLOR(_ANSI_COLOR_GREEN, x)
#define _yellow(x) _COLOR(_ANSI_COLOR_YELLOW, x)
#define _blue(x) _COLOR(_ANSI_COLOR_BLUE, x)
#define _magenta(x) _COLOR(_ANSI_COLOR_MAGENTA, x)
#define _cyan(x) _COLOR(_ANSI_COLOR_CYAN, x)
#define _dim(x) _COLOR(_ANSI_COLOR_DIM, x)
#define _bright_green(x) _COLOR(_ANSI_COLOR_BRIGHT_GREEN, x)

/* Semantic coloring helpers */
#define C_MODULE _ANSI_COLOR_GREEN
#define C_LINE_REF _ANSI_COLOR_BRIGHT_WHITE
#define C_ID _ANSI_COLOR_ORANGE
#define C_CONTROL _ANSI_COLOR_CYAN
#define C_TYPECLASS _ANSI_COLOR_CYAN
#define C_TYPE _ANSI_COLOR_YELLOW
#define C_VAR _ANSI_COLOR_GREEN
#define C_UNCHECKED _ANSI_COLOR_BRIGHT_WHITE
#define C_SIG C_TYPE
#define C_LITERAL _ANSI_COLOR_BRIGHT_WHITE
#define C_IR _ANSI_COLOR_BLUE
#define C_AST _ANSI_COLOR_BRIGHT_WHITE
#define C_FILENAME _ANSI_COLOR_BRIGHT_WHITE
#define C_ERROR _ANSI_COLOR_RED
#define C_TEXT _ANSI_COLOR_RED
#define C_PANIC _ANSI_COLOR_MAGENTA
#define C_WARN _ANSI_COLOR_YELLOW
#define C_INFO _ANSI_COLOR_BRIGHT_GREEN
#define C_GOOD _ANSI_COLOR_GREEN
#define C_INTERNAL _ANSI_COLOR_BRIGHT_WHITE
#define C_SCOPE_SEP _ANSI_COLOR_YELLOW
#define C_RESET _ANSI_COLOR_RESET
#define C_TEST_MSG _ANSI_COLOR_BRIGHT_WHITE

#define c_module(x) _COLOR(C_MODULE, x)
#define c_line_ref(x) _COLOR(C_LINE_REF, x)
#define c_id(x) _COLOR(C_ID, x)
#define c_control(x) _COLOR(C_CONTROL, x)
#define c_type(x) _COLOR(C_TYPE, x)
#define c_var(x) _COLOR(C_VAR, x)
#define c_sig(x) _COLOR(C_SIG, x)
#define c_ir(x) _COLOR(C_IR, x)
#define c_ast(x) _COLOR(C_AST, x)
#define c_unchecked(x) _COLOR(C_UNCHECKED, x)
#define c_error(x) _COLOR(C_ERROR, x)
#define c_warn(x) _COLOR(C_WARN, x)
#define c_good(x) _COLOR(C_GOOD, x)
#define c_internal(x) _COLOR(C_INTERNAL, x)
#define c_test_msg(x) _COLOR(C_TEST_MSG, x)
