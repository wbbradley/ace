#include "parse_state.h"

#include <cstdarg>

#include "ast.h"
#include "builtins.h"
#include "compiler.h"
#include "dbg.h"
#include "disk.h"
#include "logger_decls.h"
#include "parser.h"
#include "tld.h"
#include "types.h"
#include "ace.h"

namespace ace {

namespace parser {

BoundVarLifetimeTracker::BoundVarLifetimeTracker(ParseState &ps)
    : ps(ps), mutable_vars_saved(ps.mutable_vars), locals_saved(ps.locals) {
}

BoundVarLifetimeTracker::~BoundVarLifetimeTracker() {
  ps.mutable_vars = mutable_vars_saved;
  ps.locals = locals_saved;
}

TypeVarRemappingTracker::TypeVarRemappingTracker(ParseState &ps)
    : ps(ps), type_var_remapping_saved(ps.type_var_remapping) {
}

TypeVarRemappingTracker::~TypeVarRemappingTracker() {
  ps.type_var_remapping = type_var_remapping_saved;
}

const ast::Expr *BoundVarLifetimeTracker::escaped_parse_expr(
    bool allow_for_comprehensions) {
  /* pop out of the current parsing scope to allow the parser to harken back to
   * prior scopes */
  auto mutable_vars = ps.mutable_vars;
  ps.mutable_vars = mutable_vars_saved;

  auto locals = ps.locals;
  ps.locals = locals_saved;

  const ast::Expr *expr = parse_expr(ps, allow_for_comprehensions);
  ps.locals = locals;
  ps.mutable_vars = mutable_vars;
  return expr;
}

ParseState::ParseState(std::string filename,
                       std::string module_name,
                       Lexer &lexer,
                       std::vector<Token> &comments,
                       std::set<LinkIn> &link_ins,
                       SymbolExports &symbol_exports,
                       SymbolImports &symbol_imports,
                       const std::map<std::string, int> &builtin_arities)
    : filename(filename),
      module_name(module_name.size() != 0
                      ? module_name
                      : strip_ace_extension(leaf_from_file_path(filename))),
      builtin_arities(builtin_arities), lexer(lexer), comments(comments),
      link_ins(link_ins), symbol_exports(symbol_exports),
      symbol_imports(symbol_imports) {
  advance();
}

bool ParseState::advance() {
  debug_lexer(log(log_info, "advanced from %s %s", tkstr(token.tk),
                  token.text.c_str()[0] != '\n' ? token.text.c_str() : ""));
  prior_token = token;
  return lexer.get_token(token, newline, &comments);
}

Token ParseState::token_and_advance() {
  advance();
  return prior_token;
}

Identifier ParseState::identifier_and_advance(bool map_id, bool ignore_locals) {
  if (token.tk != tk_identifier) {
    throw user_error(token.location, "expected an identifier here");
  }
  advance();
  auto id = Identifier{prior_token.text, prior_token.location};
  return map_id ? id_mapped(id, ignore_locals) : id;
}

types::Ref ParseState::type_var_and_advance() {
  if (token.tk != tk_identifier) {
    throw user_error(token.location, "expected a type variable here");
  }
  advance();
  return type_variable(Identifier{type_var_remapping.count(prior_token.text)
                                      ? type_var_remapping[prior_token.text]
                                      : prior_token.text,
                                  prior_token.location});
}

Identifier ParseState::id_mapped(Identifier id, bool ignore_locals) {
  if (tld::is_fqn(id.name)) {
    /* this has already been mapped */
    return id;
  }
  if (!ignore_locals && in(id.name, locals)) {
    return id;
  }
  auto iter = module_term_map.find(id.name);
  if (iter != module_term_map.end()) {
    return Identifier{iter->second, id.location};
  } else {
    return id;
  }
}

void ParseState::error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  auto error = user_error(token.location, format, args);
  va_end(args);
  if (lexer.eof()) {
    error.add_info(token.location, "encountered end-of-file");
  }
  throw error;
}

Identifier ParseState::mkfqn(Identifier id) {
  if (tld::is_fqn(id.name)) {
    dbg();
    throw user_error(
        id.location,
        "it doesn't make sense to make a module fqn from an fqn (%s)",
        id.str().c_str());
  }

  return Identifier{tld::mktld(module_name, id.name), id.location};
}

void ParseState::export_symbol(Identifier id, Identifier fqn_id) {
  if (starts_with(id.name, "_")) {
    /* do not export any symbols with leading underscores */
    return;
  }
  auto &module_exports = symbol_exports[module_name];
  auto iter = module_exports.find(id);
  if (iter != module_exports.end()) {
    throw user_error(id.location, "duplicate symbol %s in exports",
                     id.str().c_str())
        .add_info(iter->first.location, "see previous symbol");
  }

  auto fqn_source_id = mkfqn(id);
  debug_above(2, log("ps.symbol_exports[" c_module("%s") "][%s] = %s",
                     module_name.c_str(), fqn_source_id.str().c_str(),
                     fqn_id.str().c_str()));

  module_exports[fqn_source_id] = fqn_id;
}

void ParseState::add_term_map(Location location,
                              std::string key,
                              std::string value,
                              bool allow_override) {
  debug_above(
      3, log("adding %s to module_term_map => %s", key.c_str(), value.c_str()));
  if (!allow_override && in(key, module_term_map)) {
    throw user_error(location, "symbol %s imported twice", key.c_str())
        .add_info(location, "%s was already mapped to %s", key.c_str(),
                  tld::strip_prefix(module_term_map.at(key)).c_str());
  }
  module_term_map[key] = value;
}

} // namespace parser
} // namespace ace
