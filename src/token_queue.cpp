#include "token_queue.h"
#include "utils.h"
#include "dbg.h"

struct token_matcher {
	const char *text;
	token_kind tk;
};

void zion_token_queue_t::enqueue(const location_t &location, token_kind tk) {
	zion_string_t token_text;
	enqueue(location, tk, token_text);
}

void zion_token_queue_t::enqueue(const location_t &location, token_kind tk, const zion_string_t &token_text) {
	m_last_tk = tk;
	m_queue.push_back({location, tk, token_text.str()});
}

bool zion_token_queue_t::empty() const {
   	return m_queue.empty();
}

token_t zion_token_queue_t::pop() {
	token_t token = m_queue.front();
	m_queue.pop_front();
	if (m_queue.empty()) {
		return token;
	} else {
		token_t next_token = m_queue.front();
		if (token.tk == tk_integer &&
			   	next_token.tk == tk_float &&
				token.location.line == next_token.location.line &&
				token.location.col + token.text.size() == next_token.location.col &&
			   	starts_with(next_token.text, "."))
		{
			/* combine these two tokens into a single float */
			m_queue.pop_front();
			return token_t{token.location, tk_float, token.text + next_token.text};
		} else {
			return token;
		}
	}
}

token_kind zion_token_queue_t::last_tk() const {
	return m_last_tk;
}


void zion_token_queue_t::set_last_tk(token_kind tk) {
	m_last_tk = tk;
}
