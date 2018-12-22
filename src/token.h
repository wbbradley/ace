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
	tk_space, /* " \t" */
	tk_newline, /* newline */

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

	// Literals
	tk_char, /* char literal */
	tk_error, /* error literal */
	tk_float, /* 3.1415e20 */
	tk_integer, /* [0-9]+ */
	tk_string, /* "string literal" */
	tk_about, /* @ */

	// Operators
	tk_equal, /* == */
	tk_binary_equal, /* === */
	tk_inequal, /* != */
	tk_binary_inequal, /* !== */
	tk_expr_block, /* => */
	tk_bang, /* ! */
	tk_maybe, /* ? */
	tk_lt, /* < */
	tk_gt, /* > */
	tk_lte, /* <= */
	tk_gte, /* >= */
	tk_assign, /* = */
	tk_becomes, /* := */
	tk_subtype, /* <: */
	tk_plus, /* + */
	tk_minus, /* - */
	tk_backslash, /* \ */
	tk_times, /* * */
	tk_divide_by, /* / */
	tk_mod, /* % */
	tk_pipe, /* | */
	tk_shift_left, /* << */
	tk_shift_right, /* >> */
	tk_hat, /* ^ */
	tk_dot, /* . */
	tk_double_dot, /* .. */
	tk_ampersand, /* & */

	// Mutating binary ops
	tk_plus_eq, /* += */
	tk_maybe_eq, /* ?= */
	tk_minus_eq, /* -= */
	tk_times_eq, /* *= */
	tk_divide_by_eq, /* /= */
	tk_mod_eq, /* %= */

};


#define K(x) const char * const K_##x = #x
K(_);
K(__filename__);
K(and);
K(any);
K(as);
K(break);
K(class);
K(continue);
K(defer);
K(else);
K(fn);
K(fix);
K(for);
K(get);
K(global);
K(has);
K(if);
K(in);
K(integer);
K(is);
K(lambda);
K(let);
K(link);
K(unreachable);
K(module);
K(new);
K(not);
K(or);
K(return);
K(sizeof);
K(struct);
K(tag);
K(to);
K(type);
K(var);
K(match);
K(where);
K(while);
K(with);
#undef K
#define K(x) K_##x

bool is_restricted_var_name(std::string x);

struct token_t {
	token_t(const location_t &location={{""},-1,-1}, token_kind tk=tk_none, std::string text="") : location(location), tk(tk), text(text) {}
	location_t location;
	token_kind tk = tk_none;
	std::string text;
	std::string str() const;
	void emit(int &indent_level, token_kind &last_tk, bool &indented_line);
	bool is_ident(const char *x) const;
	bool operator <(const token_t &rhs) const { return text < rhs.text; }
};

const char *tkstr(token_kind tk);
void emit_tokens(const std::vector<token_t> &tokens);
int64_t parse_int_value(token_t token);

