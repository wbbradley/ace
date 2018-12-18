#include "zion.h"
#include "dbg.h"
#include "parse_state.h"
#include "logger_decls.h"
#include <cstdarg>
#include "builtins.h"
#include "types.h"


parse_state_t::parse_state_t(
		std::string filename,
		zion_lexer_t &lexer,
		std::vector<token_t> *comments,
		std::set<token_t> *link_ins) :
	filename(filename),
	lexer(lexer),
	comments(comments),
	link_ins(link_ins)
{
	advance();
}

bool parse_state_t::advance() {
	debug_lexer(log(log_info, "advanced from %s %s", tkstr(token.tk), token.text.c_str()[0] != '\n' ? token.text.c_str() : ""));
	prior_token = token;
	return lexer.get_token(token, newline, comments);
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

void parse_state_t::add_term_map(location_t location, std::string key, std::string value) {
	if (in(key, term_map)) {
		throw user_error(location, "symbol imported twice");
	}
	term_map[key] = value;
}
