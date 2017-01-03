#pragma once
#include "stackstring.h"
#include <vector>
#include <string>
#include "location.h"

typedef stackstring_t<(1024 * 4) - sizeof(char) - sizeof(size_t)> zion_string_t;

enum token_kind
{
	tk_none, /* NULL TOKEN */

	// Comment
	tk_comment, /* # hey */

	// Whitespace
	tk_space, /* " " */
	tk_newline, /* newline */

	// Context Blocks
	tk_indent, /* indent */
	tk_outdent, /* outdent */

	// References
	tk_identifier, /* identifier */

	// Syntax
	tk_lparen, /* ( */
	tk_rparen, /* ) */
	tk_comma, /* , */
	tk_lcurly, /* { */
	tk_rcurly, /* } */
	tk_lsquare, /* [ */
	tk_rsquare, /* ] */
	tk_colon, /* : */
	tk_semicolon, /* ; */
	tk_def, /* def */
	tk_var, /* var */
	tk_return, /* return */

	// Types
	tk_any, /* any */
	tk_type, /* type */
	tk_tag, /* tag */
	tk_get_typeid, /* __get_typeid__ */
	tk_is, /* is */
	tk_has, /* has */
	tk_matches, /* matches */

	// Literals
	tk_atom, /* atom literal */
	tk_char, /* char literal */
	tk_error, /* error literal */
	tk_float, /* 3.1415e20 */
	tk_integer, /* [0-9]+ */
	tk_nil, /* null */
	tk_string, /* string literal */
	tk_version, /* #blah */

	// Flow control
	tk_pass, /* pass */
	tk_if, /* if */
	tk_elif, /* else */
	tk_else, /* else */
	tk_while, /* while */
	tk_continue, /* continue */
	tk_break, /* break */
	tk_when, /* when */

	// Operators
	tk_equal, /* == */
	tk_inequal, /* != */
	tk_bang, /* ! */
	tk_maybe, /* ? */
	tk_lt, /* < */
	tk_gt, /* > */
	tk_lte, /* <= */
	tk_gte, /* >= */
	tk_assign, /* = */
	tk_becomes, /* := */
	tk_plus, /* + */
	tk_minus, /* - */
	tk_times, /* * */
	tk_divide_by, /* / */
	tk_mod, /* % */
	tk_dot, /* . */
	tk_double_dot, /* .. */
	tk_not, /* not */
	tk_in, /* in */
	tk_or, /* or */
	tk_and, /* and */

	// Mutating binary ops
	tk_plus_eq, /* += */
	tk_maybe_eq, /* ?= */
	tk_minus_eq, /* -= */
	tk_times_eq, /* *= */
	tk_divide_by_eq, /* /= */
	tk_mod_eq, /* %= */

	// Dependency tokens
	tk_module, /* module */
	tk_link, /* link */
	tk_to, /* to */
};


struct zion_token_t {
	zion_token_t(const location &location={{""},-1,-1}, token_kind tk=tk_none, std::string text="") : location(location), tk(tk), text(text) {}
	location location;
	token_kind tk = tk_none;
	std::string text;
	std::string str() const;
	void emit(int &indent_level, token_kind &last_tk, bool &indented_line);
};

const char *tkstr(token_kind tk);
void emit_tokens(const std::vector<zion_token_t> &tokens);
char tk_char_to_char(const std::string &token_text);
