#pragma once
#include "lexer.h"
#include "ast.h"
#include <memory>
#include <sstream>
#include "parse_state.h"

template <typename T, typename... Args>
std::shared_ptr<T> parse_text(std::istream &is, std::string filename = "repl.zion") {
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
std::shared_ptr<T> parse_text(const std::string &text, std::string filename = "repl.zion") {
	std::istringstream iss(text);
	return parse_text<T>(iss, filename);
}

identifier::ref make_code_id(const token_t &token);


#define eat_token() \
	do { \
		debug_lexer(log(log_info, "eating a %s", tkstr(ps.token.tk))); \
		ps.advance(); \
	} while (0)


#define expect_token(_tk) \
	do { \
		if (ps.token.tk != _tk) { \
			ps.error("expected '%s', got '%s' " c_id("%s"), \
				   	tkstr(_tk), tkstr(ps.token.tk), ps.token.tk == tk_identifier ? ps.token.text.c_str() : ""); \
		} \
	} while (0)

#define expect_ident(text_) \
	do { \
		const char * const token_text = (text_); \
		if (ps.token.tk != tk_identifier || ps.token.text != token_text) { \
			ps.error("expected " c_id("%s") ", got " c_warn("%s"), \
					token_text, ps.token.text.size() != 0 ? ps.token.text.c_str() : tkstr(ps.token.tk)); \
		} \
	} while (0)

#define chomp_token(_tk) \
	do { \
		expect_token(_tk); \
		eat_token(); \
	} while (0)
#define chomp_ident(text_) \
	do { \
		expect_ident(text_); \
		eat_token(); \
	} while (0)

