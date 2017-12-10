#pragma once
#include <string>
#include <functional> 
#include <sstream>
#include <vector>
#include <memory>
#include <map>

struct shared_comparator {
    template <typename T>
    bool operator()(const std::shared_ptr<T> &lhs,
                    const std::shared_ptr<T> &rhs) const
    {
        return (*lhs) < (*rhs);
    }
};

void base64_encode(const void *buffer, unsigned long size, std::string &encoded_output);
bool base64_decode(const std::string &input, char * * const output, size_t * const size);

bool regex_exists(std::string input, std::string regex);
bool regex_match(std::string input, std::string regex);

std::string clean_ansi_escapes_if_not_tty(FILE *fp, std::string out);
std::string clean_ansi_escapes(std::string out);
std::string string_formatv(const std::string fmt_str, va_list args);
std::string string_format(const std::string fmt_str, ...);
std::string base26(unsigned int i);
void strrev(char *p);

template <typename T>
T merge(const T &a, const T &b) {
	T new_t;
	for (auto i : a) {
		new_t[i.first] = i.second;
	}
	for (auto i : b) {
		new_t[i.first] = i.second;
	}
	return new_t;
}

template <typename T>
struct maybe {
	T t;
	bool valid;

	template <typename U>
	friend std::ostream &operator <<(std::ostream &os, const maybe<U> &m);

	maybe() {}
	maybe(const T &&t) : t(std::move(t)), valid(true) {}
	maybe(const T &t) : t(t), valid(true) {}
	maybe(const T *rhs) : valid(bool(rhs != nullptr)) {
		if (rhs) {
			t = *rhs;
		}
	}
	maybe(const maybe<T> &mt) : t(mt.t), valid(mt.valid) {}
	maybe(const maybe<T> &&mt) : t(std::move(mt.t)), valid(mt.valid) {}

	const T *as_ptr() const {
		return valid ? &t : nullptr;
	}
};

template <typename T>
std::ostream &operator <<(std::ostream &os, const maybe<T> &m) {
	if (m.valid) {
		return os << m.t;
	} else {
		return os;
	}
}

template <typename U, typename V>
std::vector<U> keys(const std::map<U, V> &map) {
	std::vector<U> k;
	for (auto it=map.begin(); it != map.end(); ++it) {
		k.push_back(it->first);
	}
	return k;
}

bool starts_with(const std::string &str, const std::string &search);
bool starts_with(const char *str, const std::string &search);
bool ends_with(const std::string &str, const std::string &search);
std::vector<std::string> split(std::string data, std::string delim=" ");


template <typename T>
inline size_t countof(const T &t) {
	return t.size();
}

template <typename T, size_t N>
constexpr size_t countof(T (&array)[N]) {
	return N;
}

inline int mask(int grf, int grf_mask) {
	return grf & grf_mask;
}

template<class InputIt, class UnaryPredicate>
InputIt find_if(InputIt first, InputIt last, UnaryPredicate p)
{
	for (; first != last; ++first) {
		if (p(*first)) {
			return first;
		}
	}
	return last;
}

static inline std::string &ltrim(std::string &s) {
	s.erase(s.begin(), ::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
	return s;
}

static inline std::string &rtrim(std::string &s) {
	s.erase(::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
	return s;
}

static inline std::string &trim(std::string &s) {
	return ltrim(rtrim(s));
}

template <typename X>
std::string join(const X &xs, std::string delim) {
	std::stringstream ss;
	const char *sep = "";
	for (const auto &x : xs) {
		ss << sep << x;
		sep = delim.c_str();
	}
	return ss.str();
}

template <typename X>
std::string join_str(const X &xs, std::string delim) {
	std::stringstream ss;
	const char *sep = "";
	for (const auto &x : xs) {
		ss << sep << x->str();
		sep = delim.c_str();
	}
	return ss.str();
}

template <typename X>
std::string join_str(X begin, X end, std::string delim) {
	std::stringstream ss;
	const char *sep = "";
	for (X iter=begin; iter != end; ++iter) {
		ss << sep << (*iter)->str();
		sep = delim.c_str();
	}
	return ss.str();
}

template <typename X, typename F>
std::string join_with(const X &xs, std::string delim, F f) {
	std::stringstream ss;
	const char *sep = "";
	for (const auto &x : xs) {
		ss << sep << f(x);
		sep = delim.c_str();
	}
	return ss.str();
}

inline const char *boolstr(bool x) {
	return x ? "true" : "false";
}

std::vector<std::string> readlines(std::string filename);
bool real_path(std::string filename, std::string &real_path);
std::string get_cwd();
std::string escape_json_quotes(const std::string &s);
void escape_json_quotes(std::ostream &ss, const std::string &str);
std::string unescape_json_quotes(const std::string &s);
size_t utf8_sequence_length(char ch_);

template <typename U, typename COLL>
bool in(U item, const COLL &set) {
	return set.find(item) != set.end();
}

