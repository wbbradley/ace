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
  std::vector<Token> comments;
  std::set<LinkIn> link_ins;

  ParseState ps(filename, "<text>", lexer, comments, link_ins,
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

bool token_begins_type(const Token &token);
inline Identifier iid(const Token &token) {
  return Identifier::from_token(token);
}

#define eat_token()                                                            \
  do {                                                                         \
    debug_lexer(log(log_info, "eating a %s", tkstr(ps.token.tk)));             \
    ps.advance();                                                              \
  } while (0)

#define expect_token(_tk)                                                      \
  do {                                                                         \
    if (ps.token.tk != _tk) {                                                  \
      dbg();                                                                   \
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

#define maybe_chomp_token(_tk)                                                 \
  ((ps.token.tk == (_tk)) ? (ps.advance(), true) : (false))
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

const bitter::Module *parse_module(
    ParseState &ps,
    std::vector<const bitter::Module *> auto_import_modules,
    std::set<Identifier> &module_deps);

types::Ref parse_type(ParseState &ps, bool allow_top_level_application);
const bitter::Expr *parse_literal(ParseState &ps);
const bitter::Expr *parse_expr(ParseState &ps);
const bitter::Expr *parse_assignment(ParseState &ps);
const bitter::Expr *parse_tuple_expr(ParseState &ps);
const bitter::Expr *parse_let(ParseState &ps, Identifier var_id, bool is_let);
const bitter::Expr *parse_block(ParseState &ps, bool expression_means_return);
const bitter::Conditional *parse_if(ParseState &ps);
const bitter::While *parse_while(ParseState &ps);
const bitter::Expr *parse_lambda(ParseState &ps);
const bitter::Match *parse_match(ParseState &ps);
const bitter::Predicate *parse_predicate(ParseState &ps,
                                         bool allow_else,
                                         maybe<Identifier> name_assignment);
const bitter::Predicate *unfold_application_into_predicate(
    const bitter::Application *application);
const bitter::Predicate *convert_expr_to_predicate(const bitter::Expr *expr);
