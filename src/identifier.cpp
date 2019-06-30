#include "identifier.h"

#include <sstream>

#include "dbg.h"
#include "zion.h"

Identifier::Identifier(const std::string &name, Location location)
    : name(name), location(location) {
  assert(name.size() != 0);
}

std::string str(const Identifiers &ids) {
  return std::string("[") + join(ids, ", ") + "]";
}

std::set<Identifier> to_set(const Identifiers &identifiers) {
  std::set<Identifier> set;
  std::for_each(identifiers.begin(), identifiers.end(),
                [&set](Identifier x) { set.insert(x); });
  return set;
}

std::set<std::string> to_atom_set(const Identifiers &refs) {
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
