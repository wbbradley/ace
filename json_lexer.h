#pragma once
#include <istream>
#include "stackstring.h"

enum json_token_kind {
	jtk_lbrace,
	jtk_rbrace,
	jtk_comma,
	jtk_colon,
	jtk_string,
	jtk_whitespace,
	jtk_number,
	jtk_lbracket,
	jtk_rbracket,
	jtk_true,
	jtk_false,
	jtk_null,
	jtk_error
};

const char *jtkstr(json_token_kind jtk);

typedef stackstring_t<(1024 * 4) - sizeof(char) - sizeof(size_t)> json_string_t;

class json_lexer {
public:
	json_lexer(std::istream &sock_is, bool skip_comment = false);
	~json_lexer();

	bool get_token();

	json_token_kind current_jtk() const;
	const json_string_t &current_text() const;
	void advance();
private:
	void reset_token();

	std::istream             &m_sock_is;
	json_string_t             m_token_text;
	json_token_kind           m_jtk;
	bool                      m_valid_token;
	bool                      m_skip_comment;
};

size_t utf8_sequence_length(char ch);
