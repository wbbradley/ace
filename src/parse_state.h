#pragma once
#include <string>

#include "identifier.h"
#include "lexer.h"
#include "link_ins.h"
#include "logger_decls.h"
#include "ptr.h"
#include "scope.h"
#include "types.h"
#include "user_error.h"

namespace zion {
struct ParseState {
  typedef log_level_t parse_error_level_t;
  parse_error_level_t pel_error = log_error;

  ParseState(std::string filename,
             std::string module_name,
             Lexer &lexer,
             std::vector<Token> &comments,
             std::set<LinkIn> &link_ins,
             const std::map<std::string, int> &builtin_arities);

  bool advance();
  Token token_and_advance();
  Identifier identifier_and_advance();
  void error(const char *format, ...);
  void add_term_map(Location, std::string, std::string);
  void add_type_map(Location, std::string, std::string);
  bool line_broke() const {
    return newline || prior_token.tk == tk_semicolon;
  }

  const std::map<std::string, int> &builtin_arities;
  Identifier id_mapped(Identifier id);
  Token token;
  Token prior_token;
  std::string filename;
  std::string module_name;
  Lexer &lexer;

  /* top-level term remapping from "get" statements */
  std::unordered_map<std::string, std::string> term_map;

  std::vector<Token> &comments;
  std::set<LinkIn> &link_ins;
  CtorIdMap ctor_id_map;
  DataCtorsMap data_ctors_map;
  types::TypeEnv type_env;

  /* scoped expression contexts */
  std::list<Scope> scopes;

  bool is_mutable_var(std::string name);

  /* keep track of the current function declaration parameter position */
  int argument_index;

  /* enable sugaring of literals */
  bool sugar_literals = true;

private:
  bool newline = false;
};

} // namespace zion
