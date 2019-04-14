#pragma once
#include <set>
#include <string>
#include <vector>

#include "colors.h"
#include "location.h"
#include "token.h"
#include "utils.h"

struct identifier_t {
  std::string const name;
  location_t const location;
  static identifier_t from_token(token_t token);
  inline token_t get_token() const {
    return token_t{location, tk_identifier, name};
  }
  std::string str() const {
    return string_format(c_id("%s"), name.c_str());
  }

  bool operator<(const identifier_t &rhs) const;
};
using identifiers_t = std::vector<identifier_t>;

std::string str(identifiers_t ids);

#define make_iid(name_)                                                        \
  identifier_t {                                                               \
    name_, location_t {                                                        \
      __FILE__, __LINE__, 1                                                    \
    }                                                                          \
  }

namespace std {
template <> struct hash<identifier_t> {
  int operator()(const identifier_t &s) const {
    /* location is not a disambiguator for identifiers */
    return std::hash<std::string>()(s.name);
  }
};
} // namespace std

std::set<identifier_t> to_set(const identifiers_t &identifiers);
std::set<std::string> to_atom_set(const identifiers_t &refs);

std::ostream &operator<<(std::ostream &os, const identifier_t &rhs);
