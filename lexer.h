#pragma once
#include "token.h"
#include "token_queue.h"

#ifdef DEBUG_LEXER
#define debug_lexer(x) x
#else
#define debug_lexer(x)
#endif

struct token_pair
{
	token_kind tk;
	std::string text;
};

class zion_lexer_t
{
public:
	zion_lexer_t(atom filename, std::istream &sock_is);
	~zion_lexer_t();

	bool get_token(zion_token_t &token, bool &newline, std::vector<zion_token_t> *comments);
	bool _get_tokens();

private:
	void reset_token();
	void enqueue_indents(int line, int col, int indent_depth);
	bool handle_nests(token_kind tk);
	void pop_nested(token_kind tk);

	atom                      m_filename;
	std::istream             &m_is;
	int                       m_line=1, m_col=1;
	int                       m_last_indent_depth;
	zion_token_queue_t       m_token_queue;
	std::list<token_kind>     m_nested_tks;
};
