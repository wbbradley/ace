#include "token.h"

#include <sstream>
#include <unordered_set>

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

bool is_assignment_operator(const Token& token) {
  static const std::unordered_set<std::string> assignment_operators = {
    "=",
    "+=",
    "-=",
    "*=",
    "/=",
    "%=",
  };
  switch (token.tk) {
  case tk_operator:
    return in(token.text, assignment_operators);
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
    tk_case(operator);
    tk_case(char);
    tk_case(comma);
    tk_case(comment);
    tk_case(error);
    tk_case(float);
    tk_case(identifier);
    tk_case(integer);
    tk_case(lcurly);
    tk_case(lparen);
    tk_case(lsquare);
    tk_case(newline);
    tk_case(none);
    tk_case(rcurly);
    tk_case(rparen);
    tk_case(rsquare);
    tk_case(semicolon);
    tk_case(space);
    tk_case(string);
    tk_case(string_expr_continuation);
    tk_case(string_expr_prefix);
    tk_case(string_expr_suffix);
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

bool Token::is_dot_ident() const {
  return tk == tk_identifier && starts_with(text, ".");
}

bool Token::is_oper(const char *x) const {
  return tk == tk_operator && text == x;
}

bool Token::is_oper_like(const char *x) const {
  return tk == tk_operator && starts_with(text, x);
}

bool Token::operator<(const Token &rhs) const {
  return text < rhs.text;
}

bool Token::follows_after(const Token &a) const {
  return location.col == int(a.location.col + a.text.size()) &&
         location.line == a.location.line;
}

double parse_float_value(Token token) {
  double value;
  std::istringstream iss(token.text);
  iss >> value;
  if (value != value) {
    throw user_error(token.location, "%s is not a number", token.text.c_str());
  }
  return value;
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
