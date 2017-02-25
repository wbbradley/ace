#include "dbg.h"
#include "assert.h"
#include <stdlib.h>
#include <string>
#include <sstream>
#include <regex>
#include "logger_decls.h"
#include <unistd.h>
#include <limits.h>
#include <cstdarg>

#define SWP(x,y) (x^=y, y^=x, x^=y)

void strrev(char *p) {
	char *q = p;
	while(q && *q) ++q; /* find eos */
	for(--q; p < q; ++p, --q) SWP(*p, *q);
}

std::string base26(unsigned int i) {
	char buf[32] = {0};
	std::stringstream ss;
	buf[15] = 0;
	int pos = 15;
	do {
		--pos;
		buf[pos] = ('a' + (i % 26));
		i /= 26;
	} while (i != 0);
	return &buf[pos];
}

char hexdigit(int ch)
{
	assert(ch < 16);
	assert(ch >= 0);
	if (ch >= 10)
		return 'A' + ch - 10;
	return '0' + ch;
}

char hexdigit_lc(int ch)
{
	assert(ch < 16);
	assert(ch >= 0);
	if (ch >= 10)
		return 'a' + ch - 10;
	return '0' + ch;
}

char base64(int i) {
	const char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	if (i < 0 || i >= 64) {
		assert(false);
		return '*';
	}
	return base64[i];
}

void base64_encode(const void *payload, unsigned long payload_size, std::string &encoding) {
	/* rfc 1521 */
	const unsigned char *octets = reinterpret_cast<const unsigned char *>(payload);
	const size_t len = payload_size;
	std::stringstream ss;

	/* For each group of 3 bytes */
	for (size_t i = 0; i < len; i += 3) {
		ss << base64(octets[i] >> 2);
		if (i + 1 < len) {
			ss << base64(((octets[i] & 3) << 4) + (octets[i + 1] >> 4));

			if (i + 2 < len) {
				ss << base64(((octets[i + 1] & 0xf) << 2) + (octets[i + 2] >> 6));
				ss << base64((octets[i + 2] & 0x3f));
			} else {
				ss << base64(((octets[i + 1] & 0xf) << 2));
			}
		} else {
			ss << base64((octets[i] & 3) << 4);
		}
		size_t remainder = ((i + 3) % len);
		if (remainder == 1) {
			ss << "=";
		} else if (remainder == 2) {
			ss << "==";
		}
	}
	encoding = ss.str();
}

const static char ch_pad = '=';

bool base64_decode(const std::string &input, char ** const output, size_t * const size) {
	if (input.size() % 4) //Sanity check
	{
		log(log_error, "invalid base64");
		return false;
	}

	size_t padding = 0;
	if (input.size() != 0)
	{
		if (input[input.size() - 1] == ch_pad)
			padding++;
		if (input[input.size() - 2] == ch_pad)
			padding++;
	}
	else
	{
		log(log_warning, "zero sized base64 data");
		return false;
	}

	*size = ((input.size() / 4) * 3) - padding;
	(*output) = (char*)malloc(*size);
	uint8_t *pch = (uint8_t *)(*output);
	const uint8_t *pch_end = pch + (*size);
	uint32_t temp = 0;
	auto cursor = input.begin();
	while (true)
	{
		for (size_t quantumPosition = 0; quantumPosition < 4; ++quantumPosition) {
			temp <<= 6;
			if (*cursor >= 0x41 && *cursor <= 0x5A) {
				temp |= *cursor - 0x41;
			} else if (*cursor >= 0x61 && *cursor <= 0x7A) {
				temp |= *cursor - 0x47;
			} else if (*cursor >= 0x30 && *cursor <= 0x39) {
				temp |= *cursor + 0x04;
			} else if (*cursor == 0x2B) {
				temp |= 0x3E;
			} else if (*cursor == 0x2F) {
				temp |= 0x3F;
			} else if (*cursor == ch_pad) {
				switch (input.end() - cursor) {
				case 1:
					/* one pad character */
					*pch++ = (temp >> 16) & 0x000000FF;
					if (pch >= pch_end) {
						assert(false);
						return false;
					}
					*pch++ = (temp >> 8) & 0x000000FF;
					if (pch != pch_end) {
						assert(false);
						return false;
					}
					return true;
				case 2: //Two pad characters
					*pch++ = (temp >> 10) & 0x000000FF;
					if (pch != pch_end) {
						assert(false);
						return false;
					}
					return true;
				default:
					assert(false);
					return false;
				}
			}
			else {
				assert(false);
				return false;
			}
			++cursor;
		}
		*pch++ = (temp >> 16) & 0x000000FF;
		if (pch >= pch_end) {
			assert(false);
			return false;
		}
		*pch++ = (temp >> 8 ) & 0x000000FF;
		if (pch >= pch_end) {
			assert(false);
			return false;
		}
		*pch++ = (temp) & 0x000000FF;
		if (cursor == input.end()) {
			if (pch != pch_end) {
				assert(false);
				return false;
			}
			return true;
		}
		if (pch >= pch_end) {
			assert(false);
			return false;
		}
	}
	return false;
}

bool regex_exists(std::string input, std::string regex_) {
	std::smatch match;
	return std::regex_search(input, match, std::regex(regex_.c_str()));
}

bool regex_match(std::string input, std::string regex_) {
	return std::regex_match(input, std::regex(regex_.c_str()));
}

std::string string_formatv(const std::string fmt_str, va_list args_) {
    int final_n, n = ((int)fmt_str.size()) * 2; /* Reserve two times as much as the length of the fmt_str */
    std::string str;
    std::unique_ptr<char[]> formatted;
	va_list args;
    while(1) {
        formatted.reset(new char[n]); /* Wrap the plain char array into the unique_ptr */
        strcpy(&formatted[0], fmt_str.c_str());
		va_copy(args, args_);
        final_n = vsnprintf(&formatted[0], n, fmt_str.c_str(), args);
        if (final_n < 0 || final_n >= n)
            n += abs(final_n - n + 1);
        else
            break;
    }
    return std::string(formatted.get());
}

std::string string_format(const std::string fmt_str, ...) {
	va_list args;
	va_start(args, fmt_str);
	std::string str = string_formatv(fmt_str, args);
	va_end(args);
	return str;
}

std::string clean_ansi_escapes(std::string out) {
	enum {ok, in_esc} state = ok;
	size_t insert = 0;
	for (size_t read = 0; read < out.size(); ++read) {
		char ch = out[read];
		switch (state) {
		case ok:
			if (ch == '\x1b') {
				state = in_esc;
			} else {
				if (read != insert) {
					out[insert] = ch;
				}
				++insert;
			}
			break;
		case in_esc:
			if (ch == 'm') {
				state = ok;
			} else {
				/* skip */
			}
			break;
		}
	}
	out.resize(insert);

	return out;
}

std::string clean_ansi_escapes_if_not_tty(FILE *fp, std::string out) {
	if (!isatty(fileno(fp)) && !getenv("COLORIZE")) {
		return clean_ansi_escapes(out);
	} else {
		return out;
	}
}

bool starts_with(const std::string &str, const std::string &search) {
	return str.find(search) == 0;
}

bool starts_with(atom atom_str, const std::string &search) {
	return atom_str.str().find(search) == 0;
}

bool starts_with(const char *str, const std::string &search) {
	return strstr(str, search.c_str()) == str;
}

bool ends_with(const std::string &str, const std::string &search) {
	return str.find(search) == str.size() - search.size();
}

std::vector<std::string> split(std::string data, std::string delim) {
	std::vector<std::string> output;
    size_t pos = std::string::npos;
    do
    {
        pos = data.find(delim);
		if (pos != 0) {
			output.push_back(data.substr(0, pos));
		}
        if (std::string::npos != pos) {
            data = data.substr(pos + delim.size());
		}
    } while (std::string::npos != pos);
    return output;
}

bool real_path(std::string filename, std::string &real_path) {
	char real_path_buf[PATH_MAX];
	if (realpath(filename.c_str(), real_path_buf)) {
		real_path = real_path_buf;
		return true;
	} else {
		return false;
	}
}

std::string get_cwd() {
	char cwd_buf[PATH_MAX];
	if (getcwd(cwd_buf, PATH_MAX - 1)) {
		return {cwd_buf};
	} else {
		panic("can't get current working directory");
		return "";
	}
}
