#include "identifier.h"

#include <sstream>

#include "dbg.h"
#include "tld.h"
#include "user_error.h"
#include "cider.h"

Identifier::Identifier(const std::string &name, Location location)
    : name(name), location(location) {
#ifdef CIDER_DEBUG
  assert(name.size() != 0);
  for (auto ch : name) {
    assert(!iscntrl(ch));
  }
  assert(name.find("0x7") == std::string::npos);
#endif
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

cider::Token Identifier::get_token() const {
  return cider::Token{location, cider::tk_identifier, name};
}

std::string Identifier::str() const {
  return string_format(c_id("%s"), cider::tld::strip_prefix(name).c_str());
}

Identifier Identifier::from_token(cider::Token token) {
  assert(token.tk == cider::tk_identifier || token.tk == cider::tk_operator);
  return Identifier{token.text, token.location};
}

bool Identifier::operator<(const Identifier &rhs) const {
  /* location is not a disambiguator for identifiers */
  return name < rhs.name;
}

std::ostream &operator<<(std::ostream &os, const Identifier &rhs) {
  return os << rhs.str();
}

bool in(std::string needle, const Identifiers &haystack) {
  for (auto &id : haystack) {
    if (needle == id.name) {
      return true;
    }
  }
  return false;
}
