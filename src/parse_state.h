#pragma once
#include <string>
#include "lexer.h"
#include "logger_decls.h"
#include "status.h"
#include "ptr.h"
#include "identifier.h"

namespace types {
	struct type_t;
}

typedef std::map<std::string, ptr<const types::type_t>> type_macros_t;

struct parse_state_t {
	typedef log_level_t parse_error_level_t;
	parse_error_level_t pel_error = log_error;

	parse_state_t(
			std::string filename,
			zion_lexer_t &lexer,
			type_macros_t type_macros,
			type_macros_t &global_type_macros,
			std::vector<token_t> *comments=nullptr,
			std::set<token_t> *link_ins=nullptr);

	bool advance();
	void error(const char *format, ...);

	bool line_broke() const { return newline || prior_token.tk == tk_semicolon; }
	token_t token;
	token_t prior_token;
	std::string filename;
	identifier::ref module_id;
	zion_lexer_t &lexer;
	type_macros_t type_macros;
	type_macros_t &global_type_macros;
	std::vector<token_t> *comments;
	std::set<token_t> *link_ins;

	/* keep track of the current function declaration parameter position */
	int argument_index;

private:
	bool newline = false;
};

struct type_macros_restorer_t {
	type_macros_restorer_t(type_macros_t &old_macros) : old_macros(old_macros), new_macros(old_macros) {
	   	old_macros.swap(new_macros);
   	}
	~type_macros_restorer_t() {
	   	new_macros.swap(old_macros);
   	}
private:
	type_macros_t &old_macros;
	type_macros_t new_macros;
};

void add_default_type_macros(type_macros_t &type_macros);
