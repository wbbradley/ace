#pragma once
#include "token.h"
#include "token_queue.h"

#ifdef DEBUG_LEXER
#define debug_lexer(x) x
#else
#define debug_lexer(x)
#endif

struct token_pair {
  token_kind tk;
  std::string text;
};

class zion_lexer_t {
public:
  zion_lexer_t(std::string filename, std::istream &sock_is);
  ~zion_lexer_t();

  bool get_token(token_t &token, bool &newline, std::vector<token_t> *comments);
  bool _get_tokens();
  bool eof();

  std::list<std::pair<location_t, token_kind>> nested_tks;

private:
  void reset_token();
  bool handle_nests(token_kind tk);
  void pop_nested(token_kind tk);

  std::string m_filename;
  std::istream &m_is;
  int m_line = 1, m_col = 1;
  zion_token_queue_t m_token_queue;
};
