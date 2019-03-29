#include <list>

#include "token.h"

struct zion_token_queue_t {
  std::list<token_t> m_queue;
  token_kind m_last_tk = tk_none;
  void enqueue(const location_t &location,
               token_kind tk,
               const zion_string_t &token_text);
  void enqueue(const location_t &location, token_kind tk);
  bool empty() const;
  token_kind last_tk() const;
  void set_last_tk(token_kind tk);
  token_t pop();
};
