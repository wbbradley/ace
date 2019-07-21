#include <list>

#include "token.h"

namespace zion {

struct TokenQueue {
  std::list<Token> m_queue;
  token_kind m_last_tk = tk_none;
  void enqueue(const Location &location,
               token_kind tk,
               const ZionString &token_text);
  void enqueue(const Location &location, token_kind tk);
  bool empty() const;
  token_kind last_tk() const;
  void set_last_tk(token_kind tk);
  Token pop();
};

} // namespace zion
