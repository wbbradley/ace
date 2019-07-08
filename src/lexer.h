#pragma once
#include "token.h"
#include "token_queue.h"

#ifdef DEBUG_LEXER
#define debug_lexer(x) x
#else
#define debug_lexer(x)
#endif

namespace zion {

struct token_pair {
  token_kind tk;
  std::string text;
};

class Lexer {
public:
  Lexer(std::string filename, std::istream &sock_is);
  ~Lexer();

  bool get_token(Token &token, bool &newline, std::vector<Token> *comments);
  bool _get_tokens();
  bool eof();

  std::list<std::pair<Location, token_kind>> nested_tks;

private:
  void reset_token();
  bool handle_nests(token_kind tk);
  void pop_nested(token_kind tk);

  std::string m_filename;
  std::istream &m_is;
  int m_line = 1, m_col = 1;
  TokenQueue m_token_queue;
};

} // namespace zion
