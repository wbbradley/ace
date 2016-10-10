#include "token_queue.h"

struct token_matcher {
	const char *text;
	token_kind tk;
};

token_kind translate_tk(token_kind tk, const zion_string_t &token_text) {
	static const auto token_matchers = std::vector<token_matcher>{
		{"any", tk_any},
		{"and", tk_and},
		{"break", tk_break},
		{"continue", tk_continue},
		{"def", tk_def},
		{"elif", tk_elif},
		{"else", tk_else},
		{"has", tk_has},
		{"if", tk_if},
		{"in", tk_in},
		{"is", tk_is},
		{"link", tk_link},
		{"matches", tk_matches},
		{"module", tk_module},
		{"not", tk_not},
		{"null", tk_null},
		{"or", tk_or},
		{"pass", tk_pass},
		{"return", tk_return},
		{"tag", tk_tag},
		{"to", tk_to},
		{"type", tk_type},
		{"__get_typeid__", tk_get_typeid},
		{"var", tk_var},
		{"while", tk_while},
		{"when", tk_when},
	};

	if (tk == tk_identifier) {
		for (auto &tm : token_matchers) {
			if (token_text == tm.text) {
				return tm.tk;
			}
		}
	}
	return tk;
}

void zion_token_queue_t::enqueue(const location &location, token_kind tk) {
	zion_string_t token_text;
	enqueue(location, tk, token_text);
}

void zion_token_queue_t::enqueue(const location &location, token_kind tk, const zion_string_t &token_text) {
	tk = translate_tk(tk, token_text);
	m_last_tk = tk;
	m_queue.push_back({location, tk, token_text.str()});
}

bool zion_token_queue_t::empty() const {
   	return m_queue.empty();
}

zion_token_t zion_token_queue_t::pop() {
	zion_token_t token = m_queue.front();
	m_queue.pop_front();
	return token;
}

token_kind zion_token_queue_t::last_tk() const {
	return m_last_tk;
}


void zion_token_queue_t::set_last_tk(token_kind tk) {
	m_last_tk = tk;
}
