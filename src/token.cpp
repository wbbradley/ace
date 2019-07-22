#include "token.h"

#include <sstream>

#include "dbg.h"
#include "user_error.h"
#include "zion_assert.h"

namespace zion {

bool is_restricted_var_name(std::string x) {
  static const std::string keywords[] = {
      "and",
      "as",
      "break",
      "continue",
      "fn",
      "else",
      "for",
      "if",
      "in",
      "let",
      "match",
      "not",
      "null",
      "or",
      "pass",
      "return",
      "sizeof",
      "struct",
      "var",
      "while",
  };
  for (auto k : keywords) {
    if (x == k) {
      return true;
    }
  }
  return false;
}

bool tkvisible(token_kind tk) {
  switch (tk) {
  default:
    return true;
  case tk_newline:
    return false;
  }
}

std::string Token::str() const {
  std::stringstream ss;
  if (text.size() != 0) {
    ss << C_ID << "'" << text << "'" << C_RESET;
    ss << "@";
  }
  ss << location.str();
  return ss.str();
}

#define tk_case(x)                                                             \
  case tk_##x:                                                                 \
    return #x

const char *tkstr(token_kind tk) {
  switch (tk) {
    tk_case(ampersand);
    tk_case(assign);
    tk_case(expr_block);
    tk_case(becomes);
    tk_case(char);
    tk_case(colon);
    tk_case(comma);
    tk_case(comment);
    tk_case(divide_by);
    tk_case(divide_by_eq);
    tk_case(dot);
    tk_case(double_dot);
    tk_case(equal);
    tk_case(binary_equal);
    tk_case(error);
    tk_case(float);
    tk_case(gt);
    tk_case(gte);
    tk_case(identifier);
    tk_case(inequal);
    tk_case(binary_inequal);
    tk_case(integer);
    tk_case(lcurly);
    tk_case(lparen);
    tk_case(lsquare);
    tk_case(subtype);
    tk_case(lt);
    tk_case(lte);
    tk_case(maybe);
    tk_case(bang);
    tk_case(backslash);
    tk_case(minus);
    tk_case(minus_eq);
    tk_case(mod);
    tk_case(mod_eq);
    tk_case(newline);
    tk_case(none);
    tk_case(plus);
    tk_case(pipe);
    tk_case(hat);
    tk_case(shift_left);
    tk_case(shift_right);
    tk_case(plus_eq);
    tk_case(maybe_eq);
    tk_case(rcurly);
    tk_case(rparen);
    tk_case(rsquare);
    tk_case(semicolon);
    tk_case(space);
    tk_case(string);
    tk_case(times);
    tk_case(times_eq);
    tk_case(about);
  }
  return "";
}

void ensure_space_before(token_kind prior_tk) {
  switch (prior_tk) {
  case tk_none:
  case tk_char:
  case tk_colon:
  case tk_comment:
  case tk_dot:
  case tk_double_dot:
  case tk_lcurly:
  case tk_lparen:
  case tk_lsquare:
  case tk_newline:
  case tk_rcurly:
  case tk_float:
  case tk_rparen:
  case tk_rsquare:
  case tk_space:
  case tk_maybe:
  case tk_bang:
  case tk_about:
    break;
  case tk_assign:
  case tk_expr_block:
  case tk_becomes:
  case tk_comma:
  case tk_divide_by:
  case tk_divide_by_eq:
  case tk_equal:
  case tk_binary_equal:
  case tk_error:
  case tk_gt:
  case tk_gte:
  case tk_identifier:
  case tk_inequal:
  case tk_binary_inequal:
  case tk_integer:
  case tk_subtype:
  case tk_lt:
  case tk_lte:
  case tk_maybe_eq:
  case tk_minus:
  case tk_backslash:
  case tk_minus_eq:
  case tk_mod:
  case tk_mod_eq:
  case tk_plus:
  case tk_pipe:
  case tk_hat:
  case tk_shift_left:
  case tk_shift_right:
  case tk_plus_eq:
  case tk_semicolon:
  case tk_string:
  case tk_times:
  case tk_ampersand:
  case tk_times_eq:
    printf(" ");
    break;
  }
}

void ensure_indented_line(bool &indented_line, int indent_level) {
  if (!indented_line) {
    indented_line = true;
    for (int i = 0; i < indent_level; ++i) {
      printf("\t");
    }
  }
}

bool Token::is_ident(const char *x) const {
  return tk == tk_identifier && text == x;
}

bool Token::operator<(const Token &rhs) const {
  return text < rhs.text;
}

bool Token::follows_after(const Token &a) const {
  return location.col == a.location.col + a.text.size() &&
         location.line == a.location.line;
}

int64_t parse_int_value(Token token) {
  switch (token.tk) {
  case tk_integer: {
    int64_t value;
    if (token.text.size() > 2 && token.text.substr(0, 2) == "0x") {
      /* hexadecimal */
      value = strtoll(token.text.substr(2).c_str(), nullptr, 16);
    } else if (token.text.size() >= 2 && token.text[0] == '0') {
      /* octal */
      value = strtoll(token.text.substr(1).c_str(), nullptr, 8);
    } else {
      /* decimal */
      value = atoll(token.text.c_str());
    }
    return value;
  }
  default:
    throw zion::user_error(token.location,
                           "unable to read an integer value from %s",
                           token.str().c_str());
  }
}

} // namespace zion
