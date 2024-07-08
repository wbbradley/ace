#include "lexer.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "dbg.h"
#include "logger_decls.h"
#include "user_error.h"
#include "utils.h"
#include "cider.h"

namespace cider {

Lexer::Lexer(std::string filename, std::istream &sock_is)
    : m_filename(filename), m_is(sock_is) {
}

bool istchar_start(char ch) {
  return isalpha(ch) || ch == '_';
}

bool istchar(char ch) {
  if (istchar_start(ch))
    return true;

  if (isdigit(ch))
    return true;

  return false;
}

bool isophead(int ch) {
  switch (ch) {
  case '!':
  case '$':
  case '%':
  case '&':
  case '*':
  case '+':
  case '-':
  case '.':
  case '/':
  case '<':
  case '=':
  case '>':
  case '?':
  case '@':
  case '\\':
  case '^':
  case '|':
  case '~':
    return true;
  default:
    return false;
  }
}

bool isoptail(int ch) {
  return isophead(ch);
}

bool Lexer::eof() {
  return m_is.eof();
}

bool Lexer::get_token(Token &token,
                      bool &newline,
                      std::vector<Token> *comments) {
  newline = false;
  do {
    /* look ahead 3 tokens */
    while (m_token_queue.m_queue.size() < 3) {
      if (!_get_tokens()) {
        debug_lexer(log(log_info, "lexer - done reading input."));
        return false;
      }
    }

    assert(!m_token_queue.empty());

    token = m_token_queue.pop();

    if (token.tk == tk_newline) {
      newline = true;
    } else {
      // log_location(token.location, "%s", token.str().c_str());
    }

    if (comments != nullptr && token.tk == tk_comment) {
      comments->push_back(token);
    }
  } while (token.tk == tk_newline || token.tk == tk_space ||
           token.tk == tk_comment);

  debug_lexer(log(log_info, "lexed (%s) \"%s\"@%s", tkstr(token.tk),
                  token.text.c_str(), token.location().c_str()));
  return token.tk != tk_none;
}

#define gts_keyword_case_ex(wor, letter, _gts)                                 \
  case gts_##wor:                                                              \
    if (ch != letter) {                                                        \
      assert(tk == tk_identifier);                                             \
      gts = gts_token;                                                         \
      scan_ahead = false;                                                      \
    } else {                                                                   \
      gts = _gts;                                                              \
    }                                                                          \
    break

#define gts_keyword_case(wor, letter, word)                                    \
  gts_keyword_case_ex(wor, letter, gts_##word)

#define gts_keyword_case_last_ex(word, _gts)                                   \
  case _gts:                                                                   \
    if (istchar(ch)) {                                                         \
      assert(tk == tk_identifier);                                             \
      gts = gts_token;                                                         \
      scan_ahead = false;                                                      \
    } else {                                                                   \
      tk = tk_##word;                                                          \
      gts = gts_end;                                                           \
      scan_ahead = false;                                                      \
    }                                                                          \
    break;

#define gts_keyword_case_last(word) gts_keyword_case_last_ex(word, gts_##word)

void advance_line_col(char ch, int &line, int &col) {
  assert(ch != EOF);
  assert(ch != 0);

  if (ch == '\n') {
    ++line;
    col = 1;
  } else {
    ++col;
  }
}

bool Lexer::_get_tokens() {
  enum gt_state {
    gts_comment,
    gts_cr,
    gts_dot,
    gts_slash,
    gts_end,
    gts_end_quoted,
    gts_error,
    gts_expon,
    gts_expon_symbol,
    gts_float,
    gts_hexadecimal,
    gts_integer,
    gts_multiline_comment,
    gts_multiline_comment_slash,
    gts_multiline_comment_star,
    gts_octal,
    gts_quoted,
    gts_quoted_escape,
    gts_quoted_dollar,
    gts_single_quoted,
    gts_single_quoted_escape,
    gts_single_quoted_got_char,
    gts_start,
    gts_token,
    gts_whitespace,
    gts_zero,
    gts_zerox,
    gts_operator,
  };

  gt_state gts = gts_start;
  bool scan_ahead = true;

  char ch = 0;
  size_t sequence_length = 0;
  std::string token_text;
  TokenKind tk = tk_none;
  int line = m_line;
  int col = m_col;
  int multiline_comment_depth = 0;
  while (gts != gts_end && gts != gts_error) {
    ch = m_is.peek();

    switch (gts) {
    case gts_whitespace:
      if (ch != ' ') {
        gts = gts_end;
        scan_ahead = false;
      }
      break;
    case gts_cr:
      switch (ch) {
      case '\n':
        tk = tk_space;
        gts = gts_end;
      default:
        tk = tk_none;
        gts = gts_error;
      }
      break;
    case gts_operator:
      if (isoptail(ch)) {
        tk = tk_operator;
      } else {
        scan_ahead = false;
        gts = gts_end;
      }
      break;
    case gts_comment:
      if (ch == EOF || ch == '\r' || ch == '\n') {
        gts = gts_end;
        scan_ahead = false;
      }
      break;
    case gts_multiline_comment:
      assert(multiline_comment_depth > 0);
      if (ch == EOF) {
        throw user_error(Location{m_filename, m_line, m_col},
                         "end-of-file encountered within a multiline comment");
      } else if (ch == '*') {
        gts = gts_multiline_comment_star;
      } else if (ch == '/') {
        gts = gts_multiline_comment_slash;
      }
      break;
    case gts_multiline_comment_slash:
      assert(multiline_comment_depth >= 1);
      if (ch != '/') {
        if (ch == '*') {
          ++multiline_comment_depth;
        }
        gts = gts_multiline_comment;
      }
      break;
    case gts_multiline_comment_star:
      if (ch != '/') {
        if (ch != '*') {
          gts = gts_multiline_comment;
        } else {
          // stay in this mode
        }
      } else {
        /* this was an end comment token */
        assert(multiline_comment_depth > 0);
        if (multiline_comment_depth > 1) {
          gts = gts_multiline_comment;
        } else {
          gts = gts_end;
          tk = tk_comment;
        }
        --multiline_comment_depth;
      }
      break;
    case gts_slash:
      if (ch == '*') {
        gts = gts_multiline_comment;
        assert(multiline_comment_depth == 0);
        ++multiline_comment_depth;
      } else {
        tk = tk_operator;
        if (isoptail(ch)) {
          gts = gts_operator;
        } else {
          scan_ahead = false;
          gts = gts_end;
        }
      }
      break;
    case gts_dot:
      gts = gts_end;
      if (isdigit(ch)) {
        gts = gts_float;
      } else if (isoptail(ch)) {
        gts = gts_operator;
      } else if (istchar_start(ch)) {
        /* .foo is an identifier with prefix or postfix placement */
        gts = gts_token;
        tk = tk_identifier;
      } else {
        scan_ahead = false;
      }
      break;
    case gts_start:
      switch (ch) {
      case '/':
        gts = gts_slash;
        break;
      case '.':
        gts = gts_dot;
        tk = tk_operator;
        break;
      case '#':
        gts = gts_comment;
        tk = tk_comment;
        break;
      case ';':
        gts = gts_end;
        tk = tk_semicolon;
        break;
      case '\r':
        gts = gts_cr;
        break;
      case '\n':
        tk = tk_newline;
        gts = gts_end;
        break;
      case '\t':
        tk = tk_none;
        gts = gts_error;
        log_location(log_error, Location(m_filename, m_line, m_col),
                     "encountered a tab character (\\t) used outside of a "
                     "string literal");
        break;
      case ':':
        tk = tk_colon;
        gts = gts_end;
        break;
      case ' ':
        tk = tk_space;
        gts = gts_whitespace;
        break;
      case '\'':
        gts = gts_single_quoted;
        break;
      case '"':
        gts = gts_quoted;
        break;
      case '(':
        tk = tk_lparen;
        gts = gts_end;
        break;
      case ')':
        tk = tk_rparen;
        gts = gts_end;
        break;
      case ',':
        tk = tk_comma;
        gts = gts_end;
        break;
      case '[':
        tk = tk_lsquare;
        gts = gts_end;
        break;
      case ']':
        tk = tk_rsquare;
        gts = gts_end;
        break;
      case '{':
        tk = tk_lcurly;
        gts = gts_end;
        break;
      case '}':
        if (nested_tks.size() != 0 &&
            nested_tks.back().second == tk_string_expr_prefix) {
          gts = gts_quoted;
          break;
        }
        tk = tk_rcurly;
        gts = gts_end;
        break;
      case '0':
        tk = tk_integer;
        gts = gts_zero;
        break;
      };

      if (gts == gts_start) {
        if (ch == EOF || m_is.fail()) {
          tk = tk_none;
          gts = gts_end;
          break;
        } else if (isdigit(ch)) {
          gts = gts_integer;
          tk = tk_integer;
        } else if (istchar_start(ch)) {
          gts = gts_token;
          tk = tk_identifier;
        } else if (isophead(ch)) {
          gts = gts_operator;
          tk = tk_operator;
        } else {
          sequence_length = utf8_sequence_length(ch);
          if (sequence_length > 1) {
            /* assume any non-ascii utf-8 characters are identifier names
             * for now. */
            --sequence_length;
            gts = gts_token;
            tk = tk_identifier;
          } else {
            log_location(
                log_error, Location{m_filename, line, col},
                "unknown character parsed at start of token (0x%02x) '%c'",
                (int)ch, isprint(ch) ? ch : '?');
            gts = gts_error;
          }
        }
      }
      break;
    case gts_float:
      if (ch == 'e' || ch == 'E') {
        gts = gts_expon_symbol;
      } else if (!isdigit(ch)) {
        tk = tk_float;
        gts = gts_end;
        scan_ahead = false;
      }
      break;
    case gts_expon_symbol:
      if (ch < '1' || ch > '9') {
        gts = gts_error;
      } else {
        gts = gts_expon;
      }
      break;
    case gts_expon:
      if (!isdigit(ch)) {
        gts = gts_end;
        tk = tk_float;
        scan_ahead = false;
      }
      break;
    case gts_hexadecimal:
      if (isdigit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
        gts = gts_hexadecimal;
      } else {
        gts = gts_end;
        tk = tk_integer;
        scan_ahead = false;
      }
      break;
    case gts_octal:
      if (ch >= '0' && ch <= '7') {
      } else {
        gts = gts_end;
        tk = tk_integer;
        scan_ahead = false;
      }
      break;
    case gts_zerox:
      if (isdigit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
        gts = gts_hexadecimal;
      } else {
        gts = gts_error;
      }
      break;
    case gts_zero:
      if (ch == 'x') {
        gts = gts_zerox;
      } else if (ch == 'e') {
        gts = gts_expon_symbol;
      } else if (ch == '.') {
        assert(tk != tk_char);
        m_token_queue.enqueue(Location{m_filename, line, col}, tk, token_text);
        token_text.clear();
        col = m_col;
        gts = gts_start;
        scan_ahead = false;
      } else if (!isdigit(ch)) {
        gts = gts_end;
        scan_ahead = false;
      } else {
        gts = gts_octal;
      }
      break;
    case gts_integer:
      if (ch == 'e') {
        gts = gts_expon_symbol;
      } else if (ch == '.') {
        m_token_queue.enqueue(Location{m_filename, line, col}, tk, token_text);
        token_text.clear();
        col = m_col;
        gts = gts_start;
        scan_ahead = false;
      } else if (!isdigit(ch)) {
        gts = gts_end;
        scan_ahead = false;
      }
      break;
    case gts_token:
      if (sequence_length > 0) {
        --sequence_length;
      } else if (!istchar(ch)) {
        sequence_length = utf8_sequence_length(ch);
        if (sequence_length > 1) {
          --sequence_length;
        } else {
          assert(tk == tk_identifier);
          tk = tk_identifier;
          gts = gts_end;
          scan_ahead = false;
        }
      } else {
        /* we can transition into gts_token from other near-collisions with
         * keywords */
        scan_ahead = true;
      }
      break;

    case gts_quoted:
      if (ch == EOF) {
        throw user_error(
            Location{m_filename, m_line, m_col},
            "end-of-file encountered in the middle of a quoted string");
      } else if (sequence_length > 0) {
        --sequence_length;
      } else if (ch == '\\') {
        gts = gts_quoted_escape;
      } else if (ch == '"') {
        gts = gts_end_quoted;
      } else if (ch == '$') {
        gts = gts_quoted_dollar;
      } else {
        sequence_length = utf8_sequence_length(ch);
        if (sequence_length != 0)
          --sequence_length;
      }
      break;
    case gts_end_quoted:
      if (nested_tks.size() != 0 &&
          nested_tks.back().second == tk_string_expr_prefix &&
          token_text[0] == '}') {
        tk = tk_string_expr_suffix;
      } else {
        tk = tk_string;
      }
      gts = gts_end;
      scan_ahead = false;
      break;
    case gts_quoted_escape:
      gts = gts_quoted;
      break;
    case gts_quoted_dollar:
      if (ch == '{') {
        if (token_text[0] == '"') {
          tk = tk_string_expr_prefix;
        } else {
          assert(token_text[0] == '}');
          tk = tk_string_expr_continuation;
        }
        gts = gts_end;
      } else if (ch == '\\') {
        gts = gts_quoted_escape;
      } else if (ch == '"') {
        gts = gts_end_quoted;
        tk = tk_string;
      } else {
        gts = gts_quoted;
      }
      break;
    case gts_single_quoted:
      if (sequence_length > 0) {
        panic("not yet handling multibyte chars as tk_char - up for debate");
      } else if (ch == '\\') {
        gts = gts_single_quoted_escape;
      } else if (ch == '\'') {
        gts = gts_error;
      } else {
        gts = gts_single_quoted_got_char;
        token_text.clear();
        token_text += ch;
        scan_ahead = false;
        m_is.get(ch);
      }
      break;
    case gts_single_quoted_escape:
      gts = gts_single_quoted_got_char;
      token_text.clear();
      switch (ch) {
      case 'a':
        token_text += '\a';
        break;
      case 'b':
        token_text += '\b';
        break;
      case 'e':
        token_text += '\e';
        break;
      case 'f':
        token_text += '\f';
        break;
      case 'n':
        token_text += '\n';
        break;
      case 'r':
        token_text += '\r';
        break;
      case 't':
        token_text += '\t';
        break;
      case 'v':
        token_text += '\v';
        break;
      case '\\':
        token_text += '\\';
        break;
      case '\'':
        token_text += '\'';
        break;
      case '0':
        token_text.append(1, '\0');
        break;
      case '"':
        token_text += '"';
        break;
      case '?':
        token_text += '?';
        break;
      case 'x':
        assert(!!"handle hex-encoded chars");
        break;
      default:
        gts = gts_error;
        break;
      }
      if (gts != gts_error) {
        scan_ahead = false;
        m_is.get(ch);
      }
      break;
    case gts_single_quoted_got_char:
      if (ch != '\'') {
        gts = gts_error;
      } else {
        m_is.get(ch);
        scan_ahead = false;
        tk = tk_char;
        gts = gts_end;
      }
      break;
    case gts_error:
      log(log_warning, "token lexing error occurred, so far = (%s)",
          token_text.c_str());
      break;
    case gts_end:
      break;
    }

    if (scan_ahead && gts != gts_error) {
#ifdef CIDER_DEBUG
      char ch_old = ch;
#endif
      m_is.get(ch);
      if (ch == '\n') {
        ++m_line;
        m_col = 1;
      } else {
        ++m_col;
      }
      assert(ch == ch_old);

      token_text += ch;
    }
    scan_ahead = true;
  }

  handle_nests(tk);

  if (gts != gts_error && tk != tk_error) {
    m_token_queue.enqueue(Location{m_filename, line, col}, tk, token_text);
    return true;
  }

  return false;
}

bool Lexer::handle_nests(TokenKind tk) {
  bool was_empty = nested_tks.empty();

  switch (tk) {
  case tk_string_expr_continuation:
    if (was_empty || nested_tks.back().second != tk_string_expr_prefix) {
      throw user_error(Location{m_filename, m_line, m_col},
                       "misplaced string expression continuation");
    }
    break;
  case tk_string_expr_prefix:
  case tk_lsquare:
  case tk_lparen:
  case tk_lcurly:
    nested_tks.push_back({Location(m_filename, m_line, m_col - 1), tk});
    break;
  case tk_rsquare:
    pop_nested(tk_lsquare);
    break;
  case tk_rparen:
    pop_nested(tk_lparen);
    break;
  case tk_rcurly:
    pop_nested(tk_lcurly);
    break;
  case tk_string_expr_suffix:
    pop_nested(tk_string_expr_prefix);
    break;
  default:
    break;
  }
  return !was_empty;
}

void Lexer::pop_nested(TokenKind tk) {
  auto back_tk = nested_tks.size() > 0 ? nested_tks.back().second : tk_none;
  if (back_tk == tk) {
    nested_tks.pop_back();
  } else if (back_tk != tk) {
    log_location(
        log_error,
        nested_tks.size() == 0 ? Location{m_filename, m_line, m_col - 1}
                               : nested_tks.back().first,
        "detected unbalanced brackets %s != %s", tkstr(back_tk), tkstr(tk));
  }
}

Lexer::~Lexer() {
}

} // namespace cider
