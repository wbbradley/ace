#pragma once
#include <memory>
#include <sstream>

#include "ast.h"
#include "lexer.h"
#include "parse_state.h"

std::map<std::string, int> get_builtin_arities();

template <typename T, typename... Args>
std::shared_ptr<T> parse_text(std::istream &is,
                              std::string filename = "repl.zion") {
  zion_lexer_t lexer(filename, is);
  std::vector<token_t> comments;
  std::set<token_t> link_ins;

  parse_state_t ps(filename, "<text>", lexer, comments, link_ins,
                   get_builtin_arities());

  auto item = T::parse(ps);
  if (ps.token.tk != tk_none) {
    return nullptr;
  }
  return item;
}

template <typename T, typename... Args>
std::shared_ptr<T> parse_text(const std::string &text,
                              std::string filename = "repl.zion") {
  std::istringstream iss(text);
  return parse_text<T>(iss, filename);
}

bool token_begins_type(const token_t &token);
inline identifier_t iid(const token_t &token) {
  return identifier_t::from_token(token);
}

#define eat_token()                                                            \
  do {                                                                         \
    debug_lexer(log(log_info, "eating a %s", tkstr(ps.token.tk)));             \
    ps.advance();                                                              \
  } while (0)

#define expect_token(_tk)                                                      \
  do {                                                                         \
    if (ps.token.tk != _tk) {                                                  \
      ps.error("expected '%s', got '%s' " c_id("%s"), tkstr(_tk),              \
               tkstr(ps.token.tk),                                             \
               ps.token.tk == tk_identifier ? ps.token.text.c_str() : "");     \
    }                                                                          \
  } while (0)

#define expect_ident(text_)                                                    \
  do {                                                                         \
    const char *const token_text = (text_);                                    \
    if (ps.token.tk != tk_identifier || ps.token.text != token_text) {         \
      ps.error("expected " c_id("%s") ", got " c_warn("%s"), token_text,       \
               ps.token.text.size() != 0 ? ps.token.text.c_str()               \
                                         : tkstr(ps.token.tk));                \
    }                                                                          \
  } while (0)

#define chomp_token(_tk)                                                       \
  do {                                                                         \
    expect_token(_tk);                                                         \
    eat_token();                                                               \
  } while (0)
#define chomp_ident(text_)                                                     \
  do {                                                                         \
    expect_ident(text_);                                                       \
    eat_token();                                                               \
  } while (0)

bitter::module_t *parse_module(
    parse_state_t &ps,
    std::vector<bitter::module_t *> auto_import_modules,
    std::set<identifier_t> &module_deps);

types::type_t::ref parse_type(parse_state_t &ps);
bitter::expr_t *parse_literal(parse_state_t &ps);
bitter::expr_t *parse_expr(parse_state_t &ps);
bitter::expr_t *parse_assignment(parse_state_t &ps);
bitter::expr_t *parse_tuple_expr(parse_state_t &ps);
bitter::expr_t *parse_let(parse_state_t &ps, identifier_t var_id, bool is_let);
bitter::expr_t *parse_block(parse_state_t &ps, bool expression_means_return);
bitter::conditional_t *parse_if(parse_state_t &ps);
bitter::while_t *parse_while(parse_state_t &ps);
bitter::expr_t *parse_lambda(parse_state_t &ps);
bitter::match_t *parse_match(parse_state_t &ps);
bitter::predicate_t *parse_predicate(parse_state_t &ps,
                                     bool allow_else,
                                     maybe<identifier_t> name_assignment);

