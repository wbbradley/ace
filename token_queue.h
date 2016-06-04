#include "token.h"
#include <list>

struct zion_token_queue_t {
	std::list<zion_token_t>  m_queue;
	token_kind                m_last_tk = tk_nil;
	void enqueue(const location &location, token_kind tk, const zion_string_t &token_text);
	void enqueue(const location &location, token_kind tk);
	bool empty() const;
	token_kind last_tk() const;
	void set_last_tk(token_kind tk);
	zion_token_t pop();
};


