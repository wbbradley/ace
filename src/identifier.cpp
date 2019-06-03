#include "identifier.h"

#include <sstream>

#include "dbg.h"
#include "zion.h"

identifier_t::identifier_t(const std::string &name, location_t location)
    : name(name), location(location) {
}

std::string str(const identifiers_t &ids) {
  return std::string("[") + join(ids, ", ") + "]";
}

std::set<identifier_t> to_set(const identifiers_t &identifiers) {
  std::set<identifier_t> set;
  std::for_each(identifiers.begin(), identifiers.end(),
                [&set](identifier_t x) { set.insert(x); });
  return set;
}

std::set<std::string> to_atom_set(const identifiers_t &refs) {
  std::set<std::string> set;
  for (auto ref : refs) {
    set.insert(ref.name);
  }
  return set;
}

identifier_t identifier_t::from_token(Token token) {
  assert(token.tk == tk_identifier);
  return {token.text, token.location};
}

bool identifier_t::operator<(const identifier_t &rhs) const {
  /* location is not a disambiguator for identifiers */
  return name < rhs.name;
}

std::ostream &operator<<(std::ostream &os, const identifier_t &rhs) {
  return os << rhs.str();
}
