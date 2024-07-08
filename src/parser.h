#pragma once
#include <memory>
#include <sstream>

#include "ast.h"
#include "lexer.h"
#include "parse_state.h"

namespace cider {
namespace parser {

std::map<std::string, int> get_builtin_arities();

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

#define expect_operator(text_)                                                 \
  do {                                                                         \
    const char *const token_text = (text_);                                    \
    if (ps.token.tk != tk_operator || ps.token.text != token_text) {           \
      ps.error("expected operator " c_id("%s") ", got " c_warn("%s"),          \
               token_text,                                                     \
               ps.token.text.size() != 0 ? ps.token.text.c_str()               \
                                         : tkstr(ps.token.tk));                \
    }                                                                          \
  } while (0)

#define expect_text(text_)                                                     \
  do {                                                                         \
    std::string token_text = (text_);                                          \
    if (ps.token.text != token_text) {                                         \
      ps.error("expected \"%s\", got " c_warn("%s"), token_text.c_str(),       \
               ps.token.text.size() != 0 ? ps.token.text.c_str()               \
                                         : tkstr(ps.token.tk));                \
    }                                                                          \
  } while (0)

#define maybe_chomp_text(text_)                                                \
  ((ps.token.text == (text_)) ? (ps.advance(), true) : (false))
#define maybe_chomp_token(_tk)                                                 \
  ((ps.token.tk == (_tk)) ? (ps.advance(), true) : (false))
#define chomp_operator(op)                                                     \
  do {                                                                         \
    expect_token(tk_operator);                                                 \
    if (ps.token.text == (op)) {                                               \
      eat_token();                                                             \
    }                                                                          \
  } while (0)
#define maybe_chomp_operator(op)                                               \
  ((ps.token.tk == tk_operator && ps.token.text == (op))                       \
       ? (ps.advance(), true)                                                  \
       : (false))
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
#define chomp_text(text_)                                                      \
  do {                                                                         \
    expect_text(text_);                                                        \
    eat_token();                                                               \
  } while (0)

const ast::Module *parse_module(
    ParseState &ps,
    std::vector<const ast::Module *> auto_import_modules,
    std::set<Identifier> &module_deps);

types::Ref parse_type(ParseState &ps, bool allow_top_level_application);
const ast::Expr *parse_literal(ParseState &ps);
const ast::Expr *parse_expr(ParseState &ps, bool allow_for_comprehensions);
const ast::Expr *parse_assignment(ParseState &ps);
const ast::Expr *parse_tuple_expr(ParseState &ps);
const ast::Expr *parse_let(ParseState &ps, Identifier var_id, bool is_let);
const ast::Expr *parse_block(ParseState &ps, bool expression_means_return);
const ast::Expr *parse_if(ParseState &ps);
const ast::While *parse_while(ParseState &ps);
const ast::Expr *parse_string_literal(const Token &token, TrackedTypes *typing);
const ast::Expr *parse_lambda(ParseState &ps,
                              std::string start_param_list = "(",
                              std::string end_param_list = ")");
const ast::Match *parse_match(ParseState &ps);
const ast::Predicate *parse_predicate(ParseState &ps,
                                      bool allow_else,
                                      maybe<Identifier> name_assignment,
                                      bool allow_var_refs = true);
const ast::Expr *parse_postfix_chain(ParseState &ps, const ast::Expr *expr);
const ast::Predicate *unfold_application_into_predicate(
    const ast::Application *application);
const ast::Predicate *convert_expr_to_predicate(const ast::Expr *expr);

} // namespace parser
} // namespace cider
