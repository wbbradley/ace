#pragma once
#include <string>
#include <vector>

#include "location.h"
#include "stackstring.h"

namespace zion {

enum TokenKind {
  tk_none, /* NULL TOKEN */

  // Comment
  tk_comment, /* # hey */

  // Whitespace
  tk_space,   /* " \t" */
  tk_newline, /* newline */

  // References
  tk_identifier, /* identifier */
  tk_operator,   /* operator (ie: +, -, <, <$>, |>, etc..) */
  /*
   * TODO: Flesh this out more in a doc.
   *
   * User Defined Operators can be:
   *
   * /[.=<>+-*|/\\$^@&%]+/
   * except for any tk_* that already exist:
   * (=> ! ? : = += -= *= /= %=)
   *
   * Operator precedence is based on first character of operator, in the
   * following descending priority order.
   *
   * ( "?" ":"
   *   "or"
   *   "and"
   *   "not"
   *   "<", ">", "<=", ">=", "==", "!=", "in"
   *   "<.*", ">.*", "=.+", "!.+" (user-defined)
   *   "|"
   *   "^"
   *   "&
   *   "<<", ">>"
   *
   */

  // Syntax
  tk_lparen,    /* ( */
  tk_rparen,    /* ) */
  tk_comma,     /* , */
  tk_lcurly,    /* { */
  tk_rcurly,    /* } */
  tk_lsquare,   /* [ */
  tk_rsquare,   /* ] */
  tk_semicolon, /* ; */
  tk_colon,     /* : */

  // Literals
  tk_char,                     /* char literal */
  tk_error,                    /* error literal */
  tk_float,                    /* 3.1415e20 */
  tk_integer,                  /* [0-9]+ */
  tk_string,                   /* "string literal" */
  tk_string_expr_prefix,       /* ".*[^\\]\${ */
  tk_string_expr_continuation, /* }.*[^\\]\${ */
  tk_string_expr_suffix,       /* }.*" */
};

#define K(x) const char *const K_##x = #x
K(_);
K(__filename__);
K(and);
K(as);
K(assert);
K(break);
K(class);
K(continue);
K(data);
K(defer);
K(default);
K(else);
K(ffi);
K(fn);
K(for);
K(has);
K(if);
K(is);
K(import);
K(in);
K(instance);
K(let);
K(link);
K(match);
K(new);
K(newtype);
K(not );
K(null);
K(or);
K(pkg);
K(return );
K(sizeof);
K(static_print);
K(struct);
K(var);
K(while);
K(with);
#undef K
#define K(x) K_##x

bool is_restricted_var_name(std::string x);

struct Token {
  Token(const Location &location = Location{{""}, -1, -1},
        TokenKind tk = tk_none,
        std::string text = "")
      : location(location), tk(tk), text(text) {
  }
  Location location;
  TokenKind tk = tk_none;
  std::string text;
  std::string str() const;

  bool is_ident(const char *x) const;
  bool is_dot_ident() const;
  bool is_oper(const char *x) const;
  bool is_oper_like(const char *x) const;
  bool operator<(const Token &rhs) const;
  bool follows_after(const Token &a) const;
};

const char *tkstr(TokenKind tk);
int64_t parse_int_value(Token token);
double parse_float_value(Token token);
bool is_assignment_operator(const Token &tk);
} // namespace zion
