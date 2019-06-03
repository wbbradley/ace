#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <unistd.h>
#include <vector>

#include "compiler.h"
#include "dbg.h"
#include "disk.h"
#include "lexer.h"
#include "logger.h"
#include "logger_decls.h"
#include "parser.h"
#include "unification.h"
#include "utils.h"

#define test_assert(x)                                                         \
  if (!(x)) {                                                                  \
    log(log_error,                                                             \
        "test_assert " c_error(#x) " failed at " c_line_ref("%s:%d"),          \
        __FILE__, __LINE__);                                                   \
    return false;                                                              \
  } else {                                                                     \
  }

const char *PASSED_TESTS_FILENAME = "tests-passed";

std::vector<token_kind> get_tks(zion_lexer_t &lexer,
                                bool include_newlines,
                                std::vector<Token> &comments) {
  std::vector<token_kind> tks;
  Token token;
  bool newline = false;
  while (lexer.get_token(token, newline, &comments)) {
    if (include_newlines && newline && token.tk != tk_rcurly) {
      tks.push_back(tk_newline);
    }
    tks.push_back(token.tk);
  }
  return tks;
}

const char *to_str(token_kind tk) {
  return tkstr(tk);
}

template <typename T> bool check_tks_match(T &expect, T &result) {
  auto e_iter = expect.begin();
  auto r_iter = result.begin();
  auto e_end = expect.end();
  auto r_end = result.end();

  while (e_iter != e_end && r_iter != r_end) {
    if (*e_iter != *r_iter) {
      log(log_error, "expected %s, but got %s", to_str(*e_iter),
          to_str(*r_iter));
      return false;
    }
    ++e_iter;
    ++r_iter;
  }

  bool e_at_end = (e_iter == e_end);
  bool r_at_end = (r_iter == r_end);
  if (e_at_end != r_at_end) {
    const char *who_ended = e_at_end ? "expected and end"
                                     : "got a premature end";
    log(log_error, "%s from list", who_ended);
    return false;
  }

  return (e_iter == e_end) && (r_iter == r_end);
}

template <typename T>
void log_list(log_level_t level, const char *prefix, T &xs) {
  std::stringstream ss;
  const char *sep = "";
  for (auto x : xs) {
    ss << sep;
    ss << to_str(x);
    sep = ", ";
  }
  log(level, "%s [%s]", prefix, ss.str().c_str());
}

bool check_lexer(std::string text,
                 std::vector<token_kind> expect_tks,
                 bool include_newlines,
                 std::vector<Token> &comments) {
  std::istringstream iss(text);
  zion_lexer_t lexer("check_lexer", iss);
  std::vector<token_kind> result_tks = get_tks(lexer, include_newlines,
                                               comments);
  if (!check_tks_match(expect_tks, result_tks)) {
    log(log_info, "for text: '%s'", text.c_str());
    log_list(log_info, "expected", expect_tks);
    log_list(log_info, "got     ", result_tks);
    return false;
  }
  return true;
}

struct lexer_test_t {
  std::string text;
  std::vector<token_kind> tks;
};

typedef std::vector<lexer_test_t> lexer_tests;

bool lexer_test_comments(const lexer_tests &tests,
                         std::vector<Token> &comments,
                         bool include_newlines = false) {
  for (auto &test : tests) {
    if (!check_lexer(test.text, test.tks, include_newlines, comments)) {
      return false;
    }
  }
  return true;
}

bool lexer_test(const lexer_tests &tests, bool include_newlines = false) {
  std::vector<Token> comments;
  for (auto &test : tests) {
    if (!check_lexer(test.text, test.tks, include_newlines, comments)) {
      return false;
    }
  }
  return true;
}

/*
 * LEXER TESTS
 */
bool test_lex_comments() {
  lexer_tests tests = {
      {"a # hey", {tk_identifier}},
      {"# hey", {}},
      {"( # hey )", {tk_lparen}},
      {"( /*# hey */ )", {tk_lparen, tk_rparen}},
  };

  std::vector<Token> comments;
  if (lexer_test_comments(tests, comments)) {
    if (comments.size() != tests.size()) {
      log(log_error, "failed to find the comments");
      return false;
    } else {
      return true;
    }
  } else {
    return false;
  }
}

bool test_lex_functions() {
  lexer_tests tests = {
      {"fn A\nstatement", {tk_identifier, tk_identifier, tk_identifier}},
      {"fn", {tk_identifier}},
      {" fn", {tk_identifier}},
      {"fn ", {tk_identifier}},
      {"_def", {tk_identifier}},
      {"definitely", {tk_identifier}},
      {"fn A", {tk_identifier, tk_identifier}},
      {"fn A\n", {tk_identifier, tk_identifier}},
      {"fn A {\nstatement\nstatement\n}",
       {tk_identifier, tk_identifier, tk_lcurly, tk_identifier, tk_identifier,
        tk_rcurly}},
      {"fn A {pass}",
       {tk_identifier, tk_identifier, tk_lcurly, tk_identifier, tk_rcurly}},
  };
  return lexer_test(tests);
}

bool test_lex_module_stuff() {
  lexer_tests tests = {
      {"module modules", {tk_identifier, tk_identifier}},
      {"module modules", {tk_identifier, tk_identifier}},
      {"get foo", {tk_identifier, tk_identifier}},
  };
  return lexer_test(tests);
}

bool test_lex_operators() {
  lexer_tests tests = {
      {"and", {tk_identifier}},
      {"( ),{};[]:",
       {tk_lparen, tk_rparen, tk_comma, tk_lcurly, tk_rcurly, tk_semicolon,
        tk_lsquare, tk_rsquare, tk_colon}},
      {"or", {tk_identifier}},
      {"not", {tk_identifier}},
      {"in", {tk_identifier}},
      {"has", {tk_identifier}},
      {"not in", {tk_identifier, tk_identifier}},
      {">", {tk_gt}},
      {"<", {tk_lt}},
      {">=", {tk_gte}},
      {"<=", {tk_lte}},
      {"<a", {tk_lt, tk_identifier}},
      {">a", {tk_gt, tk_identifier}},
      {"<=a", {tk_lte, tk_identifier}},
      {">=a", {tk_gte, tk_identifier}},
      {"a << b", {tk_identifier, tk_shift_left, tk_identifier}},
      {"a >> b", {tk_identifier, tk_shift_right, tk_identifier}},
      {"^", {tk_hat}},
      {"@", {tk_about}},
      {"a|b", {tk_identifier, tk_pipe, tk_identifier}},
  };
  return lexer_test(tests);
}

bool test_lex_dependency_keywords() {
  lexer_tests tests = {
      {"to tote", {tk_identifier, tk_identifier}},
      {"link linker", {tk_identifier, tk_identifier}},
      {"module modules # ignore this", {tk_identifier, tk_identifier}},
  };
  return lexer_test(tests);
}

bool test_lex_literals() {
  lexer_tests tests = {
      {"\"hello world\\n\" 13493839", {tk_string, tk_integer}},
      {"\"\"", {tk_string}},
      {"0", {tk_integer}},
      {"0.0", {tk_float}},
      {"0x3892af0", {tk_integer}},
      {"10", {tk_integer}},
  };
  return lexer_test(tests);
}

bool test_lex_syntax() {
  lexer_tests tests = {
      {"retur not note", {tk_identifier, tk_identifier, tk_identifier}},
      {"return note not", {tk_identifier, tk_identifier, tk_identifier}},
      {"return var = == pass.pass..",
       {tk_identifier, tk_identifier, tk_assign, tk_equal, tk_identifier,
        tk_dot, tk_identifier, tk_double_dot}},
      {"not", {tk_identifier}},
      {"null", {tk_identifier}},
      {"while", {tk_identifier}},
      {"if", {tk_identifier}},
      {"when", {tk_identifier}},
      {"with", {tk_identifier}},
      {"typeid", {tk_identifier}},
      {"else", {tk_identifier}},
      {"break", {tk_identifier}},
      {"breakfast", {tk_identifier}},
      {"continue", {tk_identifier}},
      {"continually", {tk_identifier}},
      {"while true\n{ foo() }",
       {tk_identifier, tk_identifier, tk_lcurly, tk_identifier, tk_lparen,
        tk_rparen, tk_rcurly}},
      {"not in", {tk_identifier, tk_identifier}},
      {"true false", {tk_identifier, tk_identifier}},
      {" not", {tk_identifier}},
      {" nothing", {tk_identifier}},
      {" not\nnot", {tk_identifier, tk_identifier}},
      {"? + - * / %",
       {tk_maybe, tk_plus, tk_minus, tk_times, tk_divide_by, tk_mod}},
      {"+=-=*=/=%=:=?=",
       {tk_plus_eq, tk_minus_eq, tk_times_eq, tk_divide_by_eq, tk_mod_eq,
        tk_becomes, tk_maybe_eq}},
  };
  return lexer_test(tests);
}

bool test_lex_floats() {
  lexer_tests tests = {
      {"1.0", {tk_float}},
      {"1.0e1", {tk_float}},
      {"123e12 # whatever this is not here\n", {tk_float}},
      {"-123.29382974284e12", {tk_minus, tk_float}},
      {"h(3.14159265)", {tk_identifier, tk_lparen, tk_float, tk_rparen}},
  };
  return lexer_test(tests);
}

bool test_lex_types() {
  lexer_tests tests = {
      {"type x int", {tk_identifier, tk_identifier, tk_identifier}},
  };
  return lexer_test(tests);
}

const char *test_module_name = "-test-";

bool compare_texts(std::string result, std::string expect) {
  /* skips whitespace at the beginning of both strings */
  auto e_i = expect.begin();
  auto e_end = expect.end();
  auto r_i = result.begin();
  auto r_end = result.end();
  for (; e_i != e_end; ++e_i) {
    if (*e_i != ' ') {
      break;
    }
  }

  for (; r_i != r_end; ++r_i) {
    if (*r_i != ' ') {
      break;
    }
  }

  while (e_i != e_end && r_i != r_end && *e_i == *r_i) {
    ++e_i;
    ++r_i;
  }

  return e_i == e_end && r_i == r_end;
}

/*
 * PARSER TESTS
 */
