#include "zion.h"
#include "lexer.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include "dbg.h"
#include <csignal>
#include "logger_decls.h"

template <typename T, typename U>
bool all(const T &xs, U u) {
	for (auto x : xs) {
		if (x != u) {
			return false;
		}
	}
	return true;
}

zion_lexer_t::zion_lexer_t(std::string filename, std::istream &sock_is)
	: m_filename(filename), m_is(sock_is), m_last_indent_depth(0)
{
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

bool zion_lexer_t::eof() {
   	return m_is.eof();
}

bool zion_lexer_t::get_token(
		token_t &token,
		bool &newline,
		std::vector<token_t> *comments)
{
	newline = false;
	do {
		for (int i = 0; i < 2 && m_token_queue.empty(); ++i) {
			if (!_get_tokens()) {
				debug_lexer(log(log_info, "lexer - done reading input."));
				return false;
			}
		}

		assert(!m_token_queue.empty());

		token = m_token_queue.pop();

		if (token.tk == tk_newline) {
			newline = true;
		}

		if (comments != nullptr && token.tk == tk_comment) {
			comments->push_back(token);
		}
	} while (
			token.tk == tk_newline ||
			token.tk == tk_space ||
			token.tk == tk_comment);

	debug_lexer(log(log_info, "lexed (%s) \"%s\"@%s",
			   	tkstr(token.tk), token.text.c_str(),
				token.location().c_str()));
	return token.tk != tk_none;
}

#define gts_keyword_case_ex(wor, letter, _gts) \
		case gts_##wor: \
			if (ch != letter) { \
				assert(tk == tk_identifier); \
				gts = gts_token; \
				scan_ahead = false; \
			} else { \
				gts = _gts; \
			} \
			break

#define gts_keyword_case(wor, letter, word) gts_keyword_case_ex(wor, letter, gts_##word)

#define gts_keyword_case_last_ex(word, _gts) \
		case _gts: \
			if (istchar(ch)) { \
				assert(tk == tk_identifier); \
				gts = gts_token; \
				scan_ahead = false; \
			} else { \
				tk = tk_##word; \
				gts = gts_end; \
				scan_ahead = false; \
			} \
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

bool zion_lexer_t::_get_tokens() {
	/* _get_tokens should make sure there are tokens in the queue. */
	enum gt_state {
		gts_start,
		gts_end,
		gts_cr,
		gts_single_quoted,
		gts_single_quoted_got_char,
		gts_single_quoted_escape,
		gts_quoted,
		gts_quoted_escape,
		gts_end_quoted,
		gts_whitespace,
		gts_indenting,
		gts_token,
		gts_error,
		gts_colon,
		gts_bang,
		gts_integer,
		gts_hexadecimal,
		gts_zero,
		gts_zerox,
		gts_float,
		gts_float_symbol,
		gts_expon,
		gts_expon_symbol,
		gts_eq,
		gts_dot,
		gts_lt,
		gts_gt,
		gts_plus,
		gts_maybe,
		gts_minus,
		gts_times,
		gts_divide_by,
		gts_mod,
		gts_comment,
		gts_multiline_comment,
		gts_multiline_comment_star,
		gts_multiline_comment_slash,
		gts_version,
	};

	gt_state gts = gts_start;
	bool scan_ahead = true, handle_indentation = false;

	char ch = 0;
	size_t sequence_length = 0;
	zion_string_t token_text;
	token_kind tk = tk_none;
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
		case gts_indenting:
			switch (ch) {
			case '\t':
				break;
			case '\r':
			case '\n':
				tk = tk_newline;
				gts = gts_end;
				break;
			default:
				gts = gts_end;
				scan_ahead = false;
				handle_indentation = true;
				break;
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
		case gts_maybe:
			if (ch == '=') {
				gts = gts_end;
				tk = tk_maybe_eq;
			} else {
				scan_ahead = false;
				gts = gts_end;
				tk = tk_maybe;
			}
			break;
		case gts_plus:
			if (ch == '=') {
				gts = gts_end;
				tk = tk_plus_eq;
			} else {
				scan_ahead = false;
				gts = gts_end;
				tk = tk_plus;
			}
			break;
		case gts_minus:
			if (ch == '=') {
				gts = gts_end;
				tk = tk_minus_eq;
			} else {
				scan_ahead = false;
				gts = gts_end;
				tk = tk_minus;
			}
			break;
		case gts_times:
			if (ch == '=') {
				gts = gts_end;
				tk = tk_times_eq;
			} else {
				scan_ahead = false;
				gts = gts_end;
				tk = tk_times;
			}
			break;
		case gts_divide_by:
			if (ch == '*') {
				gts = gts_multiline_comment;
				assert(multiline_comment_depth == 0);
				++multiline_comment_depth;
			} else if (ch == '=') {
				gts = gts_end;
				tk = tk_divide_by_eq;
			} else {
				scan_ahead = false;
				gts = gts_end;
				tk = tk_divide_by;
			}
			break;
		case gts_mod:
			if (ch == '=') {
				gts = gts_end;
				tk = tk_mod_eq;
			} else {
				scan_ahead = false;
				gts = gts_end;
				tk = tk_mod;
			}
			break;
		case gts_colon:
			if (ch == '=') {
				gts = gts_end;
				tk = tk_becomes;
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
				gts = gts_error;
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
		case gts_version:
			if (ch == EOF || isspace(ch)) {
				gts = gts_end;
				scan_ahead = false;
			}
			break;
		case gts_dot:
			gts = gts_end;
			if (isdigit(ch)) {
				gts = gts_float_symbol;
			} else if (ch == '.') {
				tk = tk_double_dot;
			} else {
				scan_ahead = false;
			}
			break;
		case gts_gt:
			gts = gts_end;
			if (ch == '=') {
				tk = tk_gte;
			} else if (ch == '>') {
				tk = tk_shift_right;
			} else {
				scan_ahead = false;
			}
			gts = gts_end;
			break;
		case gts_lt:
			gts = gts_end;
			if (ch == '=') {
				tk = tk_lte;
			} else if (ch == '<') {
				tk = tk_shift_left;
			} else {
				scan_ahead = false;
			}
			break;
		case gts_bang:
            gts = gts_end;
			if (ch == '=') {
                tk = tk_inequal;
			} else {
                scan_ahead = false;
			}
			break;
		case gts_start:
			switch (ch) {
			case '?':
				gts = gts_maybe;
				tk = tk_maybe;
				break;
			case '!':
				gts = gts_bang;
				tk = tk_bang;
				break;
			case '/':
				gts = gts_divide_by;
				break;
			case '*':
				gts = gts_times;
				break;
			case '%':
				gts = gts_mod;
				break;
			case '|':
				gts = gts_end;
				tk = tk_pipe;
				break;
			case '^':
				gts = gts_end;
				tk = tk_hat;
				break;
			case '-':
				gts = gts_minus;
				break;
			case '+':
				gts = gts_plus;
				break;
			case '@':
				gts = gts_version;
				tk = tk_version;
				break;
			case '#':
				gts = gts_comment;
				tk = tk_comment;
				break;
			case '.':
				gts = gts_dot;
				tk = tk_dot;
				break;
			case ';':
				gts = gts_end;
				tk = tk_semicolon;
				break;
			case '&':
				gts = gts_end;
				tk = tk_ampersand;
				break;
			case ':':
				gts = gts_colon;
				tk = tk_colon;
				break;
			case '\r':
				gts = gts_cr;
				break;
			case '\n':
				tk = tk_newline;
				gts = gts_end;
				break;
			case '\t':
				gts = gts_indenting;
				break;
			case ' ':
				tk = tk_space;
				gts = gts_whitespace;
				break;
			case '=':
				tk = tk_assign;
				gts = gts_eq;
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
				tk = tk_rcurly;
				gts = gts_end;
				break;
			case '<':
				tk = tk_lt;
				gts = gts_lt;
				break;
			case '>':
				tk = tk_gt;
				gts = gts_gt;
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
				} else {
					sequence_length = utf8_sequence_length(ch);
					if (sequence_length > 1) {
						/* assume any non-ascii utf-8 characters are identifier names
						 * for now. */
						--sequence_length;
						gts = gts_token;
						tk = tk_identifier;
					} else {
						log(log_warning, "unknown character parsed at start of token (0x%02x) '%c'",
								(int)ch,
								isprint(ch) ? ch : '?');
						gts = gts_error;
					}
				}
			}
			break;
		case gts_eq:
			gts = gts_end;
			if (ch == '=') {
				tk = tk_equal;
			} else {
				scan_ahead = false;
			}
			break;
		case gts_float_symbol:
			if (!isdigit(ch)) {
				gts = gts_error;
			} else {
				gts = gts_float;
			}
			break;
		case gts_float:
			if (ch == 'e' || ch == 'E') {
				gts = gts_expon_symbol;
			} else if (ch == 'r') {
				tk = tk_raw_float;
				gts = gts_end;
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
			if (ch == 'r') {
				tk = tk_raw_float;
				gts = gts_end;
			} else if (!isdigit(ch)) {
				gts = gts_end;
				tk = tk_float;
				scan_ahead = false;
			}
			break;
        case gts_hexadecimal:
            if (isdigit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
                gts = gts_hexadecimal;
            } else if (ch == 'r') {
                gts = gts_end;
                tk = tk_raw_integer;
            } else {
                gts = gts_end;
                tk = tk_integer;
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
            } else if (ch == 'r') {
                tk = tk_raw_integer;
                gts = gts_end;
            } else if (ch == 'e') {
                gts = gts_expon_symbol;
            } else if (ch == '.') {
                gts = gts_float_symbol;
            } else if (!isdigit(ch)) {
                gts = gts_end;
                scan_ahead = false;
            } else {
                gts = gts_error;
            }
            break;
		case gts_integer:
			if (ch == 'e') {
				gts = gts_expon_symbol;
			} else if (ch == '.') {
				gts = gts_float_symbol;
			} else if (ch == 'r') {
				tk = tk_raw_integer;
				gts = gts_end;
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
					assert(tk = tk_identifier);
					tk = tk_identifier;
					gts = gts_end;
					scan_ahead = false;
				}
			} else {
				/* we can transition into gts_token from other near-collisions with keywords */
				scan_ahead = true;
			}
			break;

		case gts_quoted:
			if (ch == EOF) {
				gts = gts_error;
			} else if (sequence_length > 0) {
				--sequence_length;
			} else if (ch == '\\') {
				gts = gts_quoted_escape;
			} else if (ch == '"') {
				tk = tk_string;
				gts = gts_end_quoted;
			} else {
				sequence_length = utf8_sequence_length(ch);
				if (sequence_length != 0)
					--sequence_length;
 			}
			break;
		case gts_end_quoted:
			gts = gts_end;
			if (ch == 'r') {
				tk = tk_raw_string;
			} else {
				scan_ahead = false;
			}
			break;
		case gts_quoted_escape:
			gts = gts_quoted;
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
 			}
			break;
		case gts_single_quoted_escape:
			gts = gts_single_quoted_got_char;
			break;
		case gts_single_quoted_got_char:
			if (ch != '\'') {
				gts = gts_error;
			} else {
				tk = tk_char;
				gts = gts_end;
			}
			break;
		case gts_error:
			log(log_warning, "token lexing error occurred, so far = (%s)", token_text.c_str());
			break;
		case gts_end:
			break;
		}

		if (scan_ahead && gts != gts_error) {
#ifdef ZION_DEBUG
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

			if (!m_is.fail() && !token_text.append(ch)) {
				log(log_error, "symbol too long? [line %d: col %d]",
						m_line, m_col);
				return false;
			}
		}
	}

	bool nested = handle_nests(tk);
	if (nested) {
		if (tk == tk_newline) {
			tk = tk_space;
		}
	}

	if (gts != gts_error && tk != tk_error) {
		if (m_token_queue.last_tk() == tk_newline
			   	&& !handle_indentation) {
			if (tk != tk_newline) {
				enqueue_indents(m_line, m_col, 0);
			}
		}

		if (handle_indentation) {
			/* handle standard tab indents */
			int indent_depth = token_text.size();
			debug_lexer(log(log_info, "indent_depth = %d, %d",
						indent_depth, m_last_indent_depth));
			assert(all(token_text, '\t'));
			bool empty_line = (ch == '\r' && ch == '\n');
			if (!empty_line) {
				enqueue_indents(m_line, m_col, indent_depth);
			}
		} else {
			if (tk != tk_none) {
				m_token_queue.enqueue({m_filename, line, col}, tk, token_text);
			}
		}

		if (ch == EOF) {
			enqueue_indents(m_line, m_col, 0);
			m_token_queue.enqueue({m_filename, line, col}, tk_none, token_text);
		}

		return true;
	}

	return false;
}

bool zion_lexer_t::handle_nests(token_kind tk) {
	bool was_empty = m_nested_tks.empty();

	switch (tk) {
	case tk_lsquare:
	case tk_lparen:
	case tk_lcurly:
		m_nested_tks.push_back(tk);
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
	default:
		break;
	}
	return !was_empty;
}

void zion_lexer_t::pop_nested(token_kind tk) {
	auto back_tk = m_nested_tks.size() > 0 ? m_nested_tks.back() : tk_none;
	if (back_tk == tk) {
		m_nested_tks.pop_back();
	} else if (back_tk != tk) {
		debug_lexer(log(log_error, "detected unbalanced %s%s",
				   	tkstr(back_tk), tkstr(tk)));
	}
}

void zion_lexer_t::enqueue_indents(int line, int col, int indent_depth) {
	debug_lexer(log(log_info, "enqueue_indents(%d)", indent_depth));
	if (indent_depth > m_last_indent_depth) {
		if (!m_nested_tks.size()) {
			// Handle indents
			assert(indent_depth - 1 == m_last_indent_depth);
			m_token_queue.enqueue({m_filename, line, col}, tk_indent);
			m_last_indent_depth = indent_depth;
		}
	} else if (indent_depth < m_last_indent_depth) {
		// We're outdenting
		for (int i = m_last_indent_depth; i > indent_depth; --i) {
			m_token_queue.enqueue({m_filename, line, col}, tk_outdent);
		}
		m_last_indent_depth = indent_depth;
	} else {
		m_token_queue.set_last_tk(tk_none);
	}
}

zion_lexer_t::~zion_lexer_t() {
}
