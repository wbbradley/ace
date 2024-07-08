#include "token_queue.h"

#include "dbg.h"
#include "utils.h"

namespace ace {

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
    /* some lazy hackery */
    Token next_token = m_queue.front();
    if (token.tk == tk_integer && next_token.tk == tk_float &&
        next_token.follows_after(token) && starts_with(next_token.text, ".")) {
      /* combine these two tokens into a single float */
      m_queue.pop_front();
      return Token{token.location, tk_float, token.text + next_token.text};
    } else if (token.tk == tk_lparen && next_token.tk == tk_operator &&
               next_token.follows_after(token)) {
      m_queue.pop_front();
      Token next_next_token = m_queue.front();
      if (next_next_token.tk == tk_rparen &&
          next_next_token.follows_after(next_token)) {
        m_queue.pop_front();
        return Token{next_token.location, tk_identifier, next_token.text};
      } else {
        m_queue.push_front(next_token);
      }
    }
    return token;
  }
}

TokenKind TokenQueue::last_tk() const {
  return m_last_tk;
}

void TokenQueue::set_last_tk(TokenKind tk) {
  m_last_tk = tk;
}

} // namespace ace
