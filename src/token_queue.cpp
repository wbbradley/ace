#include "token_queue.h"

#include "dbg.h"
#include "utils.h"

namespace zion {

struct token_matcher {
  const char *text;
  TokenKind tk;
};

void TokenQueue::enqueue(const Location &location, TokenKind tk) {
  std::string token_text;
  enqueue(location, tk, token_text);
}

void TokenQueue::enqueue(const Location &location,
                         TokenKind tk,
                         const std::string &token_text) {
  m_last_tk = tk;
  m_queue.push_back({location, tk, token_text});
}

bool TokenQueue::empty() const {
  return m_queue.empty();
}

Token TokenQueue::pop() {
  Token token = m_queue.front();
  m_queue.pop_front();
  if (m_queue.empty()) {
    return token;
  } else {
    Token next_token = m_queue.front();
    if (token.tk == tk_integer && next_token.tk == tk_float &&
        token.location.line == next_token.location.line &&
        int(token.location.col + token.text.size()) ==
            next_token.location.col &&
        starts_with(next_token.text, ".")) {
      /* combine these two tokens into a single float */
      m_queue.pop_front();
      return Token{token.location, tk_float, token.text + next_token.text};
    } else {
      return token;
    }
  }
}

TokenKind TokenQueue::last_tk() const {
  return m_last_tk;
}

void TokenQueue::set_last_tk(TokenKind tk) {
  m_last_tk = tk;
}

} // namespace zion
