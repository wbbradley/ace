#pragma once
#include <set>
#include <string>
#include <vector>

#include "colors.h"
#include "location.h"
#include "token.h"
#include "utils.h"

struct Identifier {
  Identifier() = default;
  Identifier(const std::string &name, Location location);
  std::string const name;
  Location const location;
  static Identifier from_token(zion::Token token);
  zion::Token get_token() const;
  std::string str() const;

  bool operator<(const Identifier &rhs) const;
};
using Identifiers = std::vector<Identifier>;

std::string str(Identifiers ids);

#define make_iid(name_)                                                        \
  Identifier {                                                                 \
    name_, Location {                                                          \
      __FILE__, __LINE__, 1                                                    \
    }                                                                          \
  }

namespace std {
template <> struct hash<Identifier> {
  int operator()(const Identifier &s) const {
    /* location is not a disambiguator for identifiers */
    return std::hash<std::string>()(s.name);
  }
};
} // namespace std

std::set<Identifier> to_set(const Identifiers &identifiers);
std::set<std::string> to_atom_set(const Identifiers &refs);

std::ostream &operator<<(std::ostream &os, const Identifier &rhs);

bool in(std::string needle, const Identifiers &haystack);
