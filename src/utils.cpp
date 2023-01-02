#include <cstdarg>
#include <ctype.h>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <regex>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <unistd.h>

#include "dbg.h"
#include "logger_decls.h"
#include "user_error.h"
#include "zion_assert.h"

#define SWP(x, y) (x ^= y, y ^= x, x ^= y)

std::string to_upper(std::string x) {
  std::string copy = x;
  for (auto &ch : copy) {
    ch = std::toupper(ch);
  }
  return copy;
}

size_t utf8_sequence_length(char ch_) {
  unsigned char ch = (unsigned char &)ch_;
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

void strrev(char *p) {
  char *q = p;
  while (q && *q)
    ++q; /* find eos */
  for (--q; p < q; ++p, --q)
    SWP(*p, *q);
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

char hexdigit(int ch) {
  assert(ch < 16);
  assert(ch >= 0);
  if (ch >= 10)
    return 'A' + ch - 10;
  return '0' + ch;
}

char hexdigit_lc(int ch) {
  assert(ch < 16);
  assert(ch >= 0);
  if (ch >= 10)
    return 'a' + ch - 10;
  return '0' + ch;
}

char base64(int i) {
  const char base64[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (i < 0 || i >= 64) {
    assert(false);
    return '*';
  }
  return base64[i];
}

void base64_encode(const void *payload,
                   unsigned long payload_size,
                   std::string &encoding) {
  /* rfc 1521 */
  const unsigned char *octets = reinterpret_cast<const unsigned char *>(
      payload);
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

bool base64_decode(const std::string &input,
                   char **const output,
                   size_t *const size) {
  if (input.size() % 4) // Sanity check
  {
    log(log_error, "invalid base64");
    return false;
  }

  size_t padding = 0;
  if (input.size() != 0) {
    if (input[input.size() - 1] == ch_pad)
      padding++;
    if (input[input.size() - 2] == ch_pad)
      padding++;
  } else {
    log(log_warning, "zero sized base64 data");
    return false;
  }

  *size = ((input.size() / 4) * 3) - padding;
  (*output) = (char *)malloc(*size);
  uint8_t *pch = (uint8_t *)(*output);
  const uint8_t *pch_end = pch + (*size);
  uint32_t temp = 0;
  auto cursor = input.begin();
  while (true) {
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
        case 2: // Two pad characters
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
      } else {
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
    *pch++ = (temp >> 8) & 0x000000FF;
    if (pch >= pch_end) {
      assert(false);
      return false;
    }
    *pch++ = (temp)&0x000000FF;
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

bool regex_lift_match(std::string text,
                      std::string regex_,
                      std::string &match) {
  std::smatch sm;
  std::regex regex(regex_);
  if (std::regex_search(text, sm, regex)) {
    if (!sm.ready() || sm.size() <= 1) {
      return false;
    }

    match = sm[1];
    return true;
  } else {
    return false;
  }
}

std::string regex_sanitize(std::string unsafe) {
  std::regex specialChars{R"([-[\]{}()*+?.,\^$|#\s])"};

  std::string sanitized = std::regex_replace(unsafe, specialChars, R"(\$&)");
  debug_above(
      7, log("regex_sanitize(%s) -> %s", unsafe.c_str(), sanitized.c_str()));
  return sanitized;
}

std::string string_formatv(const std::string fmt_str, va_list args_) {
  int final_n,
      n = ((int)fmt_str.size()) *
          2; /* Reserve two times as much as the length of the fmt_str */
  std::unique_ptr<char[]> formatted;
  va_list args;
  while (1) {
    formatted.reset(
        new char[n]); /* Wrap the plain char array into the unique_ptr */
                      // strcpy(&formatted[0], fmt_str.c_str());
    va_copy(args, args_);
    final_n = vsnprintf(&formatted[0], n, fmt_str.c_str(), args);
    va_end(args_);
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
  enum { ok, in_esc } state = ok;
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

std::string clean_ansi_escapes_if_not_tty(FILE *fp, const std::string &out) {
  if (!isatty(fileno(fp)) && !getenv("COLORIZE")) {
    return clean_ansi_escapes(out);
  } else {
    return out;
  }
}

bool starts_with(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) == 0;
}

bool starts_with(const char *haystack, const std::string &needle) {
  return strstr(haystack, needle.c_str()) == haystack;
}

bool ends_with(const std::string &haystack, const std::string &needle) {
  int pos = haystack.size() - needle.size();
  if (pos >= 0) {
    return haystack.find(needle, pos) == (size_t)pos;
  } else {
    return false;
  }
}

void ltrim(std::string &s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                                  [](int ch) { return !std::isspace(ch); }));
}

void rtrim(std::string &s) {
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [](int ch) { return !std::isspace(ch); })
              .base(),
          s.end());
}

void trim(std::string &s) {
  ltrim(s);
  rtrim(s);
}

std::string ltrim_copy(std::string s) {
  ltrim(s);
  return s;
}

std::string rtrim_copy(std::string s) {
  rtrim(s);
  return s;
}

std::string trim_copy(std::string s) {
  trim(s);
  return s;
}

std::vector<std::string> split(std::string data, std::string delim) {
  std::vector<std::string> output;
  size_t pos = std::string::npos;
  do {
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
  if (char *result = realpath(filename.c_str(), nullptr)) {
    real_path = result;
    free(result);
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

std::vector<std::string> readlines(std::string filename) {
  std::vector<std::string> lines;
  std::ifstream ifs(filename);
  std::string line;
  while (std::getline(ifs, line)) {
    lines.push_back(line);
  }
  return lines;
}

std::string escape_json_quotes(const std::string &str) {
  std::stringstream ss;

  escape_json_quotes(ss, str);

  assert(ss.str().size() >= str.size() + 2);
  return ss.str();
}

void escape_json_quotes(std::ostream &ss, const std::string &str) {
  ss << '"';
  const char *pch = str.c_str();
  const char *pch_end = pch + str.size();
  for (; pch != pch_end; ++pch) {
    switch (*pch) {
    case '\b':
      ss << "\\b";
      continue;
    case '\f':
      ss << "\\f";
      continue;
    case '\n':
      ss << "\\n";
      continue;
    case '\r':
      ss << "\\r";
      continue;
    case '\t':
      ss << "\\t";
      continue;
    case '\"':
      ss << "\\\"";
      continue;
    }

    if (*pch == '\\')
      ss << '\\';

    ss << *pch;
  }
  ss << '"';
}

uint32_t hexval(char ch) {
  if (ch >= 'a' && ch <= 'f')
    return 10 + ch - 'a';
  if (ch >= 'A' && ch <= 'F')
    return 10 + ch - 'A';
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  assert(false);
  return 0;
}

std::string unescape_json_quotes(const char *str, size_t len) {
  assert(len >= 2);
  assert(str[0] == '\"');
  assert(str[len - 1] == '\"');

  std::string res;
  res.reserve(len);
  bool escaped = false;
  const char *str_end = str + len;
  for (const auto *i = str + 1; i != str_end - 1; i++) {
    if (escaped) {
      escaped = false;
      switch (*i) {
      case 'b':
        res.push_back((char)'\b');
        continue;
      case 'f':
        res.push_back((char)'\f');
        continue;
      case 'n':
        res.push_back((char)'\n');
        continue;
      case 'r':
        res.push_back((char)'\r');
        continue;
      case 't':
        res.push_back((char)'\t');
        continue;
      case 'x':
        if (i + 3 <= str_end) {
          ++i;
          int val = hexval(*i++);
          val <<= 4;
          val |= hexval(*i);
          res.push_back(val);
        }
        continue;
      case 'u':
        assert(std::distance(i, str_end) >= 5);
        uint16_t ch = 0;
        i++;
        ch += hexval(*i);
        ch <<= 4;
        i++;
        ch += hexval(*i);
        ch <<= 4;
        i++;
        ch += hexval(*i);
        ch <<= 4;
        i++;
        ch += hexval(*i);
        std::string utf8_encoding;
        utf8::utf16to8(&ch, &ch + 1, back_inserter(utf8_encoding));
        res += utf8_encoding;
        continue;
      }
    } else if (*i == '\\') {
      escaped = true;
      continue;
    }

    res.push_back(*i);
  }
  return res;
}

std::string unescape_json_quotes(const std::string &str) {
  return unescape_json_quotes(str.c_str(), str.size());
}

std::string alphabetize(int i) {
  assert(i >= 0);
  std::string x;
  while (true) {
    char letter[2] = {char('a' + (i % 26)), 0};
    x = std::string(((const char *)letter)) + x;
    if (i < 26) {
      break;
    }
    i /= 26;
    i -= 1;
  }
  return x;
}

std::string join(int argc, const char *argv[]) {
  std::stringstream ss;
  const char *sep = "";
  for (int i = 0; i < argc; ++i) {
    ss << sep << argv[i];
    sep = " ";
  }
  return ss.str();
}

void check_command_line_text(Location location, std::string text) {
  for (auto ch : "`$%&()|") {
    if (text.find(ch) != std::string::npos) {
      throw zion::user_error(
          location, "illegal command-line text found in link in statement");
    }
  }
}

std::string shell_get_line(std::string command) {
  /* call a command and return the first line */
  check_command_line_text(INTERNAL_LOC(), command);
  FILE *fp = popen(command.c_str(), "r");
  if (fp == nullptr) {
    throw zion::user_error(INTERNAL_LOC(), "failed to invoke command %s",
                           command.c_str());
  }

  char *linep = nullptr;
  size_t linecap = 0;
  auto cb = getline(&linep, &linecap, fp);

  if (cb == -1) {
    throw zion::user_error(
        INTERNAL_LOC(), "failed to read output of command %s", command.c_str());
  }

  pclose(fp);
  std::string ret = (linep != nullptr) ? linep : "";
  free(linep);

  while (ret.size() > 0 && isspace(ret[ret.size() - 1])) {
    ret = ret.substr(0, ret.size() - 1);
  }
  return ret;
}

std::pair<int, std::string> shell_get_output(std::string command,
                                             bool redirect_to_stdout) {
  /* call a command and return the stdout as well as the return code. */
  check_command_line_text(INTERNAL_LOC(), command);
  if (redirect_to_stdout) {
    command = command + " 2>&1";
  }
  FILE *fp = popen(command.c_str(), "r");
  if (fp == nullptr) {
    throw zion::user_error(INTERNAL_LOC(), "failed to invoke command %s",
                           command.c_str());
  }

  const int buffer_size = 4 * 1024;
  char buffer[buffer_size + 1];
  buffer[buffer_size] = '\0';
  std::stringstream ss;
  int bytes_read = 0;

  do {
    bytes_read = fread(buffer, 1, buffer_size, fp);
    buffer[bytes_read] = '\0';
    ss << buffer;
  } while (bytes_read != 0);

  auto stat = pclose(fp);
  if (!WIFEXITED(stat)) {
    return {-1, ss.str()};
  } else {
    return {WEXITSTATUS(stat), ss.str()};
  }
}

std::string get_pkg_config(std::string flags, std::string pkg_name) {
  std::stringstream ss;
  ss << "pkg-config " << flags << " \"" << pkg_name << "\"";
  return shell_get_line(ss.str());
}

namespace ui {
void open_file(std::string filename) {
  const std::string open_command =
#ifdef __APPLE__
      "open"
#elif defined(WIN32) || defined(_WIN32) ||                                     \
    defined(__WIN32) && !defined(__CYGWIN__)
      "start"
#else
      "xdg-open"
#endif
      ;
  if (system((open_command + " " + filename).c_str())) {
    log("failed to open %s", filename.c_str());
  }
}
} // namespace ui
