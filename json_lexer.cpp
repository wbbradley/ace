#include "json_lexer.h"
#include "dbg.h"
#include "logger.h"


const char *jtkstr(json_token_kind jtk) {
	switch (jtk) {
	case jtk_colon:
		return "colon";
		break;
	case jtk_lbrace:
		return "lbrace";
	case jtk_rbrace:
		return "rbrace";
	case jtk_comma:
		return "comma";
	case jtk_string:
		return "string";
	case jtk_whitespace:
		return "whitespace";
	case jtk_number:
		return "number";
	case jtk_lbracket:
		return "lbracket";
	case jtk_rbracket:
		return "rbracket";
	case jtk_true:
		return "true";
	case jtk_false:
		return "false";
	case jtk_null:
		return "null";
	case jtk_error:
		return "error";
	};
	return "";
}

json_lexer::json_lexer(std::istream &sock_is, bool skip_comment) : m_sock_is(sock_is), m_jtk(jtk_error), m_skip_comment(skip_comment) {
	m_valid_token = false;
}

json_lexer::~json_lexer() {
}

size_t utf8_sequence_length(char ch_) {
	unsigned char ch = (unsigned char&)ch_;
	uint8_t lead = mask(0xff, ch);
	if (lead < 0x80) {
		return 1;
	} else if ((lead >> 5) == 0x6) {
		return 2;
	} else if ((lead >> 4) == 0xe) {
		return 3;
	} else if ((lead >> 3) == 0x1e) {
		return 4;
	} else {
		return 0;
	}
}

enum get_json_token_state {
	gjts_start,
	gjts_error,
	gjts_end,
	gjts_whitespace,
	gjts_minus,
	gjts_quoted,
	gjts_integer,
	gjts_zero,
	gjts_exponent,
	gjts_quoted_escape,
	gjts_decimal,
	gjts_fraction,
	gjts_exponent_digits,
	gjts_exponent_minus,
	gjts_t,
	gjts_tr,
	gjts_tru,
	gjts_f,
	gjts_fa,
	gjts_fal,
	gjts_fals,
	gjts_n,
	gjts_nu,
	gjts_nul,
};

bool json_lexer::get_token() {
	if (m_valid_token)
		return true;

	m_token_text.reset();

	char ch;
	get_json_token_state gjts = gjts_start;
	bool scan_ahead = true;

	/* UTF-8 sequence skipping counter */
	size_t sequence_length = 0;

	while (gjts != gjts_end && gjts != gjts_error) {
		ch = m_sock_is.peek();

		if (ch == EOF) {
			gjts = gjts_error;
		}

		switch (gjts) {
		case gjts_start:
			switch (ch) {
			case '\r':
			case '\n':
			case ' ':
			case '\t':
				gjts = gjts_whitespace;
				break;
			case '{':
				m_jtk = jtk_lbrace;
				gjts = gjts_end;
				break;
			case '}':
				m_jtk = jtk_rbrace;
				gjts = gjts_end;
				break;
			case ':':
				m_jtk = jtk_colon;
				gjts = gjts_end;
				break;
			case '[':
				m_jtk = jtk_lbracket;
				gjts = gjts_end;
				break;
			case ']':
				m_jtk = jtk_rbracket;
				gjts = gjts_end;
				break;
			case '"':
				gjts = gjts_quoted;
				break;
			case '-':
				gjts = gjts_minus;
				break;
			case '0':
				gjts = gjts_zero;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				gjts = gjts_integer;
				break;
			case ',':
				m_jtk = jtk_comma;
				gjts = gjts_end;
				break;
			case 'f':
				gjts = gjts_f;
				break;
			case 't':
				gjts = gjts_t;
				break;
			case 'n':
				gjts = gjts_n;
				break;
			case '/':
				if (m_skip_comment) {
					gjts = gjts_whitespace;
					break;
				}
			default:
#ifdef JSON_ZION_DEBUG
				printf("encountered unknown character \"%c\" = 0x%02x\n", ch, (int)ch);
#endif
				return false;
			}
			break;
		case gjts_n:
			if (ch == 'u') {
				gjts = gjts_nu;
			} else {
				gjts = gjts_error;
			}
			break;
		case gjts_nu:
			if (ch == 'l') {
				gjts = gjts_nul;
			} else {
				gjts = gjts_error;
			}
			break;
		case gjts_nul:
			if (ch == 'l') {
				m_jtk = jtk_null;
				gjts = gjts_end;
			} else {
				gjts = gjts_error;
			}
			break;
		case gjts_t:
			if (ch == 'r') {
				gjts = gjts_tr;
			} else {
				gjts = gjts_error;
			}
			break;
		case gjts_tr:
			if (ch == 'u') {
				gjts = gjts_tru;
			} else {
				gjts = gjts_error;
			}
			break;
		case gjts_tru:
			if (ch == 'e') {
				m_jtk = jtk_true;
				gjts = gjts_end;
			} else {
				gjts = gjts_error;
			}
			break;
		case gjts_f:
			if (ch == 'a') {
				gjts = gjts_fa;
			} else {
				gjts = gjts_error;
			}
			break;
		case gjts_fa:
			if (ch == 'l') {
				gjts = gjts_fal;
			} else {
				gjts = gjts_error;
			}
			break;
		case gjts_fal:
			if (ch == 's') {
				gjts = gjts_fals;
			} else {
				gjts = gjts_error;
			}
			break;
		case gjts_fals:
			if (ch == 'e') {
				m_jtk = jtk_false;
				gjts = gjts_end;
			} else {
				gjts = gjts_error;
			}
			break;
		case gjts_quoted:
			if (sequence_length > 0) {
				--sequence_length;
			} else if (ch == '\\') {
				gjts = gjts_quoted_escape;
			} else if (ch == '"') {
				m_jtk = jtk_string;
				gjts = gjts_end;
			} else {
				sequence_length = utf8_sequence_length(ch);
				if (sequence_length != 0)
					--sequence_length;
 			}
			break;
		case gjts_quoted_escape:
			gjts = gjts_quoted;
			break;
		case gjts_zero:
			if (ch == '.') {
				gjts = gjts_decimal;
			} else if (ch == '0') {
				return false;
			} else if (!isdigit(ch)) {
				m_jtk = jtk_number;
				gjts = gjts_end;
				scan_ahead = false;
			} else {
				assert(isdigit(ch));
				gjts = gjts_integer;
			}
			break;
		case gjts_minus:
			switch (ch) {
			case '0':
				gjts = gjts_zero;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				gjts = gjts_integer;
				break;
			case '.':
				gjts = gjts_decimal;
				break;
			default:
				gjts = gjts_error;
				break;
			}
			break;
		case gjts_integer:
			if (ch == '.') {
				gjts = gjts_decimal;
			} else if (!isdigit(ch)) {
				m_jtk = jtk_number;
				gjts = gjts_end;
				scan_ahead = false;
			}
			break;
		case gjts_decimal:
			if (isdigit(ch)) {
				gjts = gjts_fraction;
			} else {
				gjts = gjts_error;
			}
			break;
		case gjts_fraction:
			if (!isdigit(ch)) {
				if (ch == 'e' || ch == 'E') {
					gjts = gjts_exponent;
				} else {
					m_jtk = jtk_number;
					gjts = gjts_end;
					scan_ahead = false;
				}
			}
			break;
		case gjts_exponent:
			if (isdigit(ch)) {
				gjts = gjts_exponent_digits;
			} else if (ch == '-' || ch == '+') {
				gjts = gjts_exponent_minus;
			} else {
				gjts = gjts_error;
			}
			break;
		case gjts_exponent_minus:
			if (isdigit(ch)) {
				gjts = gjts_exponent_digits;
			} else {
				gjts = gjts_error;
			}
			break;
		case gjts_exponent_digits:
			if (!isdigit(ch)) {
				m_jtk = jtk_number;
				gjts = gjts_end;
				scan_ahead = false;
			}
			break;
		case gjts_whitespace:
			switch (ch) {
			case '\r':
			case '\n':
			case ' ':
			case '\t':
				break;
			case '/':
				if (m_skip_comment)
					break;
			default:
				m_jtk = jtk_whitespace;
				gjts = gjts_end;
				scan_ahead = false;
				break;
			}
			break;
		case gjts_error:
#ifdef ZION_DEBUG
			printf("json token lexing error occurred, so far = (%s)\n",
				   m_token_text.c_str());
#endif
			break;
		case gjts_end:
			break;
		}

		if (scan_ahead && gjts != gjts_error) {
#ifdef ZION_DEBUG
			char ch_old = ch;
#endif
			m_sock_is.get(ch);
			assert(ch == ch_old);

			if (gjts != gjts_error) {
				if (!m_token_text.append(ch)) {
					debug(log(log_error, "json_lexer : unable to add '%c' to m_token_text \"%s\"\n",
								(char)ch,
								m_token_text.c_str()));
					return false;
				}
			}
		}
	}

	m_valid_token = false;

	if (gjts != gjts_error) {
		assert(m_token_text.size() > 0);
		if (m_jtk != jtk_error) {
			m_valid_token = true;
#ifdef JSON_ZION_DEBUG
			log(log_info, "found token %s %s\n", jtkstr(m_jtk), m_text.c_str());
#endif
		}
	} else {
		assert(false);
	}

	return m_valid_token;
}

json_token_kind json_lexer::current_jtk() const {
	assert(m_valid_token);

	if (m_valid_token) {
		return m_jtk;
	} else {
		return jtk_error;
	}
}

const json_string_t &json_lexer::current_text() const {
	assert(m_valid_token);
	assert(m_token_text.size() > 0);
	return m_token_text;
}

void json_lexer::advance() {
	assert(m_valid_token);
	reset_token();
}

void json_lexer::reset_token() {
	m_jtk = jtk_error;
	m_token_text.reset();
	m_valid_token = false;
}

