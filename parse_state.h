#pragma once
#include <string>
#include "lexer.h"
#include "atom.h"
#include "logger_decls.h"
#include "status.h"
#include "ptr.h"

namespace types {
	struct type;
}

struct parse_state_t {
	typedef log_level_t parse_error_level_t;
	parse_error_level_t pel_error = log_error;
	parse_error_level_t pel_warning = log_warning;

	parse_state_t(status_t &status, std::string filename, zion_lexer_t &lexer, std::vector<zion_token_t> *comments=nullptr);

	bool advance();
	void warning(const char *format, ...);
	void error(const char *format, ...);

	bool line_broke() const { return newline || prior_token.tk == tk_semicolon; }
	zion_token_t token;
	zion_token_t prior_token;
	atom filename;
	zion_lexer_t &lexer;
	status_t &status;
	std::vector<zion_token_t> *comments;

	/* nullary reader macros for types */
	std::map<atom, ptr<const types::type>> type_macros;

	/* keep track of the current function declaration parameter position */
	int argument_index;

private:
	bool newline = false;
};
