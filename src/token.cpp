#include "token.h"

#include <sstream>

#include "dbg.h"
#include "user_error.h"
#include "zion_assert.h"

namespace zion {

bool is_restricted_var_name(std::string x) {
  static const std::string keywords[] = {
      "and",  "as",     "break",  "continue", "fn",  "else",  "for",
      "if",   "in",     "let",    "match",    "not", "null",  "or",
      "pass", "return", "sizeof", "struct",   "var", "while",
  };
  for (auto k : keywords) {
    if (x == k) {
      return true;
    }
  }
  return false;
}

bool is_assignment_operator(TokenKind tk) {
  switch (tk) {
  case tk_assign:
  case tk_plus_eq:
  case tk_minus_eq:
  case tk_divide_by_eq:
  case tk_mod_eq:
  case tk_times_eq:
  case tk_becomes:
    return true;
  default:
    return false;
  }
}

bool tkvisible(TokenKind tk) {
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

const char *tkstr(TokenKind tk) {
  switch (tk) {
    tk_case(about);
    tk_case(ampersand);
    tk_case(assign);
    tk_case(backslash);
    tk_case(bang);
    tk_case(becomes);
    tk_case(binary_equal);
    tk_case(binary_inequal);
    tk_case(char);
    tk_case(colon);
    tk_case(comma);
    tk_case(comment);
    tk_case(divide_by);
    tk_case(divide_by_eq);
    tk_case(dollar);
    tk_case(dot);
    tk_case(double_dot);
    tk_case(equal);
    tk_case(error);
    tk_case(expr_block);
    tk_case(float);
    tk_case(gt);
    tk_case(gte);
    tk_case(hat);
    tk_case(identifier);
    tk_case(inequal);
    tk_case(integer);
    tk_case(lcurly);
    tk_case(lparen);
    tk_case(lsquare);
    tk_case(lt);
    tk_case(lte);
    tk_case(maybe);
    tk_case(minus);
    tk_case(minus_eq);
    tk_case(mod);
    tk_case(mod_eq);
    tk_case(newline);
    tk_case(none);
    tk_case(pipe);
    tk_case(plus);
    tk_case(plus_eq);
    tk_case(rcurly);
    tk_case(rparen);
    tk_case(rsquare);
    tk_case(semicolon);
    tk_case(shift_left);
    tk_case(shift_right);
    tk_case(space);
    tk_case(string);
    tk_case(string_expr_continuation);
    tk_case(string_expr_prefix);
    tk_case(string_expr_suffix);
    tk_case(subtype);
    tk_case(tilde);
    tk_case(times);
    tk_case(times_eq);
  }
  return "";
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
  return location.col == int(a.location.col + a.text.size()) &&
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
