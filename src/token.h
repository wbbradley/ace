#pragma once
#include <string>
#include <vector>

#include "location.h"
#include "stackstring.h"

namespace zion {

typedef StackString<(1024 * 4) - sizeof(char) - sizeof(size_t)> ZionString;

enum token_kind {
  tk_none, /* NULL TOKEN */

  // Comment
  tk_comment, /* # hey */

  // Whitespace
  tk_space,   /* " \t" */
  tk_newline, /* newline */

  // References
  tk_identifier, /* identifier */

  // Syntax
  tk_lparen,    /* ( */
  tk_rparen,    /* ) */
  tk_comma,     /* , */
  tk_lcurly,    /* { */
  tk_rcurly,    /* } */
  tk_lsquare,   /* [ */
  tk_rsquare,   /* ] */
  tk_colon,     /* : */
  tk_semicolon, /* ; */

  // Literals
  tk_char,                     /* char literal */
  tk_error,                    /* error literal */
  tk_float,                    /* 3.1415e20 */
  tk_integer,                  /* [0-9]+ */
  tk_string,                   /* "string literal" */
  tk_about,                    /* @ */
  tk_string_expr_prefix,       /* ".*[^\\]\${ */
  tk_string_expr_continuation, /* }.*[^\\]\${ */
  tk_string_expr_suffix,       /* }.*" */

  // Operators
  tk_equal,          /* == */
  tk_binary_equal,   /* === */
  tk_inequal,        /* != */
  tk_binary_inequal, /* !== */
  tk_expr_block,     /* => */
  tk_bang,           /* ! */
  tk_maybe,          /* ? */
  tk_lt,             /* < */
  tk_gt,             /* > */
  tk_lte,            /* <= */
  tk_gte,            /* >= */
  tk_assign,         /* = */
  tk_becomes,        /* := */
  tk_subtype,        /* <: */
  tk_plus,           /* + */
  tk_minus,          /* - */
  tk_backslash,      /* \ */
  tk_times,          /* * */
  tk_divide_by,      /* / */
  tk_mod,            /* % */
  tk_pipe,           /* | */
  tk_shift_left,     /* << */
  tk_shift_right,    /* >> */
  tk_hat,            /* ^ */
  tk_dot,            /* . */
  tk_double_dot,     /* .. */
  tk_ampersand,      /* & */

  // Mutating binary ops
  tk_plus_eq,      /* += */
  tk_minus_eq,     /* -= */
  tk_times_eq,     /* *= */
  tk_divide_by_eq, /* /= */
  tk_mod_eq,       /* %= */

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
K(else);
K(export);
K(fn);
K(for);
K(has);
K(if);
K(import);
K(in);
K(instance);
K(let);
K(link);
K(match);
K(new);
K(newtype);
K(not);
K(null);
K(or);
K(pkg);
K(return );
K(sizeof);
K(static_print);
K(struct);
K(unreachable);
K(var);
K(while);
K(with);
#undef K
#define K(x) K_##x

bool is_restricted_var_name(std::string x);

struct Token {
  Token(const Location &location = Location{{""}, -1, -1},
        token_kind tk = tk_none,
        std::string text = "")
      : location(location), tk(tk), text(text) {
  }
  Location location;
  token_kind tk = tk_none;
  std::string text;
  std::string str() const;

  bool is_ident(const char *x) const;
  bool operator<(const Token &rhs) const;
  bool follows_after(const Token &a) const;
};

const char *tkstr(token_kind tk);
int64_t parse_int_value(Token token);
bool is_assignment_operator(token_kind tk);
} // namespace zion
