#pragma once
#include "lexer.h"
#include "ast.h"
#include <memory>
#include <sstream>
#include "parse_state.h"

template <typename T, typename... Args>
ptr<T> parse_text(std::istream &is, std::string filename = "repl.zion") {
	zion_lexer_t lexer(filename, is);
	type_macros_t global_type_macros;
	parse_state_t ps(filename, lexer, {}, global_type_macros);
	ps.module_id = make_iid("__parse_text__");

	auto item = T::parse(ps);
	if (ps.token.tk != tk_none) {
		return nullptr;
	}
	return item;
}

template <typename T, typename... Args>
ptr<T> parse_text(const std::string &text, std::string filename = "repl.zion") {
	std::istringstream iss(text);
	return parse_text<T>(iss, filename);
}

identifier::ref make_code_id(const token_t &token);


#define eat_token_or_return(fail_code) \
	do { \
		debug_lexer(log(log_info, "eating a %s", tkstr(ps.token.tk))); \
		ps.advance(); \
	} while (0)

#define eat_token() eat_token_or_return(nullptr)

#define expect_token_or_return(_tk, fail_code) \
	do { \
		if (ps.token.tk != _tk) { \
			ps.error("expected '%s', got '%s' " c_id("%s"), \
				   	tkstr(_tk), tkstr(ps.token.tk), ps.token.tk == tk_identifier ? ps.token.text.c_str() : ""); \
			dbg(); \
			return fail_code; \
		} \
	} while (0)

#define expect_token(_tk) expect_token_or_return(_tk, nullptr)

#define expect_ident_or_return(text_, fail_code) \
	do { \
		const char * const token_text = (text_); \
		expect_token_or_return(tk_identifier, fail_code); \
		if (ps.token.text != token_text) { \
			ps.error("expected '%s', got '%s'", \
					token_text, ps.token.text.c_str()); \
			dbg(); \
			return fail_code; \
		} \
	} while (0)

#define expect_ident(text_) expect_ident_or_return(text_, nullptr)

#define chomp_token_or_return(_tk, fail_code) \
	do { \
		expect_token_or_return(_tk, fail_code); \
		eat_token_or_return(fail_code); \
	} while (0)
#define chomp_token(_tk) chomp_token_or_return(_tk, nullptr)
#define chomp_ident_or_return(text_, fail_code) \
	do { \
		expect_ident_or_return(text_, fail_code); \
		eat_token_or_return(fail_code); \
	} while (0)
#define chomp_ident(text_) chomp_ident_or_return(text_, nullptr)

