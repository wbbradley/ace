#pragma once
#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "zion_assert.h"

struct shared_comparator {
  template <typename T>
  bool operator()(const std::shared_ptr<T> &lhs,
                  const std::shared_ptr<T> &rhs) const {
    return (*lhs) < (*rhs);
  }
};

template <class T> std::size_t hash_combine(std::size_t s, const T &v) {
  std::hash<T> h;
  return s ^ (h(v) + 0x9e3779b9 + (s << 6) + (s >> 2));
}

void base64_encode(const void *buffer,
                   unsigned long size,
                   std::string &encoded_output);
bool base64_decode(const std::string &input,
                   char **const output,
                   size_t *const size);

bool regex_exists(std::string input, std::string regex);
bool regex_match(std::string input, std::string regex);

std::string shell_get_line(std::string command);
std::string clean_ansi_escapes_if_not_tty(FILE *fp, const std::string &out);
std::string clean_ansi_escapes(std::string out);
std::string string_formatv(const std::string fmt_str, va_list args);
std::string string_format(const std::string fmt_str, ...);
std::string base26(unsigned int i);
void strrev(char *p);
std::string to_upper(std::string);

template <typename T> std::set<T> without(const std::set<T> &s, T v) {
  std::set<T> c = s;
  c.erase(v);
  return c;
}

template <typename T> void set_merge(T &as, const T &bs) {
  for (auto b : bs) {
    as.insert(b);
  }
}

template <typename T> T set_union(const T &as, const T &bs) {
  T t(as);
  for (auto b : bs) {
    t.insert(b);
  }
  return t;
}

template <typename T> void set_concat(T &as, const T &bs) {
  for (auto b : bs) {
    as.insert(b);
  }
}

template <typename T> T merge(const T &a, const T &b) {
  T new_t;
  for (auto i : a) {
    new_t[i.first] = i.second;
  }
  for (auto i : b) {
    new_t[i.first] = i.second;
  }
  return new_t;
}

template <typename T> T merge(const T &a, const T &b, const T &c) {
  T new_t;
  for (auto i : a) {
    new_t[i.first] = i.second;
  }
  for (auto i : b) {
    new_t[i.first] = i.second;
  }
  for (auto i : c) {
    new_t[i.first] = i.second;
  }
  return new_t;
}

template <typename T> struct maybe {
  T const t;
  bool const valid = false;

  template <typename U>
  friend std::ostream &operator<<(std::ostream &os, const maybe<U> &m);

  maybe() {
  }
  maybe(const T &&t) : t(std::move(t)), valid(true) {
  }
  maybe(const T &t) : t(t), valid(true) {
  }
  maybe(const T t, bool valid) : t(t), valid(valid) {
  }
#if 0
	maybe(const T *rhs) : valid(bool(rhs != nullptr)) {
		if (rhs) {
			t = *rhs;
		}
	}
#endif
  maybe(const maybe<T> &mt) : t(mt.t), valid(mt.valid) {
  }
  maybe(const maybe<T> &&mt) : t(std::move(mt.t)), valid(mt.valid) {
  }

  const T *as_ptr() const {
    return valid ? &t : nullptr;
  }
};

template <typename T, typename U>
std::vector<U> values(const std::map<T, U> &map) {
  std::vector<U> v;
  v.reserve(map.size());
  for (auto &pair : map) {
    v.push_back(pair.second);
  }
  return v;
}

template <typename T>
std::ostream &operator<<(std::ostream &os, const maybe<T> &m) {
  if (m.valid) {
    assert(false);
    // return os << m.t;
    return os;
  } else {
    return os;
  }
}

template <typename U, typename V>
std::vector<U> keys(const std::map<U, V> &map) {
  std::vector<U> k;
  for (auto it = map.begin(); it != map.end(); ++it) {
    k.push_back(it->first);
  }
  return k;
}

template <typename U, typename V>
std::set<U> set_keys(const std::map<U, V> &map) {
  std::set<U> k;
  for (auto it = map.begin(); it != map.end(); ++it) {
    k.insert(it->first);
  }
  return k;
}

template <typename T> std::set<T> set_diff(std::set<T> a, std::set<T> b) {
  std::set<T> diff;
  std::set_difference(a.begin(), a.end(), b.begin(), b.end(),
                      std::inserter(diff, diff.begin()));
  return diff;
}

template <typename T> std::set<T> set_diff(std::set<T> a, std::vector<T> b) {
  std::set<T> diff;
  std::set_difference(a.begin(), a.end(), b.begin(), b.end(),
                      std::inserter(diff, diff.begin()));
  return diff;
}

template <typename T>
std::set<T> set_intersect(const std::set<T> &a, const std::set<T> &b) {
  std::set<T> intersection;
  std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                        std::inserter(intersection, intersection.begin()));
  return intersection;
}

bool starts_with(const std::string &str, const std::string &search);
bool starts_with(const char *str, const std::string &search);
bool ends_with(const std::string &str, const std::string &search);
std::vector<std::string> split(std::string data, std::string delim = " ");

template <typename T> inline size_t countof(const T &t) {
  return t.size();
}

template <typename T, size_t N> constexpr size_t countof(T (&array)[N]) {
  return N;
}

inline int mask(int grf, int grf_mask) {
  return grf & grf_mask;
}

template <class InputIt, class UnaryPredicate>
InputIt find_if(InputIt first, InputIt last, UnaryPredicate p) {
  for (; first != last; ++first) {
    if (p(*first)) {
      return first;
    }
  }
  return last;
}

template <typename K, typename V, typename Comp>
V get(const std::map<K, V, Comp> &t, K k, V default_) {
  auto iter = t.find(k);
  if (iter != t.end()) {
    return iter->second;
  } else {
    return default_;
  }
}

template <typename K, typename V>
V get(const std::unordered_map<K, V> &t, K k, V default_) {
  auto iter = t.find(k);
  if (iter != t.end()) {
    return iter->second;
  } else {
    return default_;
  }
}

template <typename K1, typename K2, typename C, typename V>
V get(const C &t, K1 k1, K2 k2, V default_) {
  auto iter = t.find(k1);
  if (iter != t.end()) {
    return get(iter->second, k2, default_);
  } else {
    return default_;
  }
}

std::string join(int argc, const char *argv[], std::string delim = ", ");

template <typename X> std::string join(const X &xs, std::string delim = ", ") {
  std::stringstream ss;
  const char *sep = "";
  for (const auto &x : xs) {
    ss << sep << x;
    sep = delim.c_str();
  }
  return ss.str();
}

template <typename X>
std::string join_str(const X &xs, std::string delim = ", ") {
  std::stringstream ss;
  const char *sep = "";
  for (const auto &x : xs) {
    ss << sep << x->str();
    sep = delim.c_str();
  }
  return ss.str();
}

template <typename X>
std::string join_str(X begin, X end, std::string delim = ", ") {
  std::stringstream ss;
  const char *sep = "";
  for (X iter = begin; iter != end; ++iter) {
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

template <typename K, typename V>
bool contains_value(const std::map<K, V> &map, V value) {
  for (auto pair : map) {
    if (pair.second == value) {
      return true;
    }
  }
  return false;
}

template <typename K, typename V>
bool contains_value(const std::unordered_map<K, V> &map, V value) {
  for (auto pair : map) {
    if (pair.second == value) {
      return true;
    }
  }
  return false;
}

template <typename U, typename COLL> bool in(U item, const COLL &coll) {
  return coll.find(item) != coll.end();
}

template <typename C1, typename C2>
bool all_in(const C1 &items, const C2 &set) {
  for (auto item : items) {
    if (!in(item, set)) {
      return false;
    }
  }
  return true;
}

template <typename C1, typename C2>
bool any_in(const C1 &needles, const C2 &haystack) {
  for (auto &needle : needles) {
    if (in(needle, haystack)) {
      return true;
    }
  }
  return false;
}

template <typename V>
std::vector<V> vec_slice(const std::vector<V> &orig, int start, int lim) {
  std::vector<V> output;
  output.reserve(lim - start);
  for (int i = start; i < lim; ++i) {
    auto &v = orig[i];
    output.push_back(v);
  }
  return output;
}

template <typename V>
std::vector<V> vec_concat(const std::vector<V> &xs, const std::vector<V> &ys) {
  std::vector<V> output = xs;
  output.insert(output.end(), ys.begin(), ys.end());
  return output;
}

template <typename U, typename COLL> bool in_vector(U item, const COLL &set) {
  return std::find(set.begin(), set.end(), item) != set.end();
}

template <typename U, typename COLL, typename V>
bool in_vector_with(U item, const COLL &set, std::function<V(U)> extractor) {
  auto val = extractor(item);
  for (auto x : set) {
    if (extractor(x) == val) {
      return true;
    }
  }
  return false;
}

std::string alphabetize(int i);
std::string get_pkg_config(std::string flags, std::string pkg_name);
