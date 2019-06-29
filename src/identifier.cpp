#include "identifier.h"

#include <sstream>

#include "dbg.h"
#include "zion.h"

Identifier::Identifier(const std::string &name, Location location)
    : name(name), location(location) {
}

std::string str(const identifiers_t &ids) {
  return std::string("[") + join(ids, ", ") + "]";
}

std::set<Identifier> to_set(const identifiers_t &identifiers) {
  std::set<Identifier> set;
  std::for_each(identifiers.begin(), identifiers.end(),
                [&set](Identifier x) { set.insert(x); });
  return set;
}

std::set<std::string> to_atom_set(const identifiers_t &refs) {
  std::set<std::string> set;
  for (auto ref : refs) {
    set.insert(ref.name);
  }
  return set;
}

Identifier Identifier::from_token(Token token) {
  assert(token.tk == tk_identifier);
  return {token.text, token.location};
}

bool Identifier::operator<(const Identifier &rhs) const {
  /* location is not a disambiguator for identifiers */
  return name < rhs.name;
}

std::ostream &operator<<(std::ostream &os, const Identifier &rhs) {
  return os << rhs.str();
}
