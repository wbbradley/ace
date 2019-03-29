#include "parse_state.h"

#include <cstdarg>

#include "builtins.h"
#include "compiler.h"
#include "dbg.h"
#include "disk.h"
#include "logger_decls.h"
#include "parser.h"
#include "types.h"
#include "zion.h"

parse_state_t::parse_state_t(std::string filename,
                             std::string module_name,
                             zion_lexer_t &lexer,
                             std::vector<token_t> &comments,
                             std::set<token_t> &link_ins,
                             const std::map<std::string, int> &builtin_arities)
    : filename(filename),
      module_name(module_name.size() != 0
                      ? module_name
                      : strip_zion_extension(leaf_from_file_path(filename))),
      lexer(lexer), comments(comments), link_ins(link_ins),
      builtin_arities(builtin_arities) {
  advance();
}

bool parse_state_t::advance() {
  debug_lexer(log(log_info, "advanced from %s %s", tkstr(token.tk),
                  token.text.c_str()[0] != '\n' ? token.text.c_str() : ""));
  prior_token = token;
  return lexer.get_token(token, newline, &comments);
}

token_t parse_state_t::token_and_advance() {
  advance();
  return prior_token;
}

identifier_t parse_state_t::identifier_and_advance() {
  assert(token.tk == tk_identifier);
  advance();
  assert(prior_token.tk == tk_identifier);
  return id_mapped(identifier_t{prior_token.text, prior_token.location});
}

identifier_t parse_state_t::id_mapped(identifier_t id) {
  auto iter = term_map.find(id.name);
  if (iter != term_map.end()) {
    return identifier_t{iter->second, id.location};
  } else {
    return id;
  }
}

bool parse_state_t::is_mutable_var(std::string name) {
  for (auto iter = scopes.rbegin(); iter != scopes.rend(); ++iter) {
    if ((*iter).id.name == name) {
      return (*iter).is_let;
    }
  }
  return false;
}

void parse_state_t::error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  auto error = user_error(token.location, format, args);
  va_end(args);
  if (lexer.eof()) {
    error.add_info(token.location, "encountered end-of-file");
  }
  throw error;
}

void parse_state_t::add_term_map(location_t location,
                                 std::string key,
                                 std::string value) {
  // log("adding %s to term map => %s", key.c_str(), value.c_str());
  if (in(key, term_map)) {
    throw user_error(location, "symbol imported twice");
  }
  term_map[key] = value;
}
