#include "token_queue.h"

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
	return token;
}

token_kind zion_token_queue_t::last_tk() const {
	return m_last_tk;
}


void zion_token_queue_t::set_last_tk(token_kind tk) {
	m_last_tk = tk;
}
