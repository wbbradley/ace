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
namespace parser {

typedef std::map<std::string, std::map<Identifier, Identifier>> SymbolExports;
typedef std::map<std::string, std::map<std::string, std::set<Identifier>>>
    SymbolImports;

typedef std::map<std::string, types::Map> ParsedDataCtorsMap;
typedef std::unordered_map<std::string, int> ParsedCtorIdMap;

struct ParseState {
  typedef LogLevel ParseError_level;
  ParseError_level pel_error = log_error;

  ParseState(std::string filename,
             std::string module_name,
             Lexer &lexer,
             std::vector<Token> &comments,
             std::set<LinkIn> &link_ins,
             SymbolExports &symbol_exports,
             SymbolImports &symbol_imports,
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

  Identifier id_mapped(Identifier id);
  Token token;
  Token prior_token;
  std::string filename;
  std::string module_name;
  std::unordered_set<std::string> mutable_vars;

  /* top-level term remapping from "import" statements */
  std::unordered_map<std::string, std::string> term_map;

  const std::map<std::string, int> &builtin_arities;
  Lexer &lexer;
  std::vector<Token> &comments;
  std::set<LinkIn> &link_ins;
  SymbolExports &symbol_exports;
  SymbolImports &symbol_imports;
  ParsedCtorIdMap ctor_id_map;
  ParsedDataCtorsMap data_ctors_map;
  types::TypeEnv type_env;

  /* keep track of the current function declaration parameter position */
  int argument_index;

  /* enable sugaring of literals */
  bool sugar_literals = true;

private:
  bool newline = false;
};

struct BoundVarLifetimeTracker {
  BoundVarLifetimeTracker(ParseState &ps);
  ~BoundVarLifetimeTracker();

  void escaped_parse(std::function<void()> action);
private:
  ParseState &ps;
  std::unordered_set<std::string> mutable_vars_saved;
  std::unordered_map<std::string, std::string> term_map_saved;
};

} // namespace parser
} // namespace zion
