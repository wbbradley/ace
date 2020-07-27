#include "scheme_resolver.h"

#include "dbg.h"
#include "unification.h"
#include "user_error.h"

namespace types {

SchemeResolver::SchemeResolver(const SchemeResolver *parent) : parent(parent) {
}

bool SchemeResolver::scheme_exists(std::string name) const {
  return (state.count(name) == 1) ||
         (parent != nullptr && parent->scheme_exists(name));
}

void SchemeResolver::insert_scheme(std::string name,
                                   const types::SchemeRef &scheme) {
  if (state.count(name) != 0) {
    debug_above(3,
                log("attempt to insert scheme %s for preexisting name %s :: %s",
                    scheme->str().c_str(), name.c_str(),
                    state.at(name)->str().c_str()));
    assert(scheme_equality(state.at(name), scheme));
  }

  debug_above(5, log("SchemeResolver::insert_scheme(%s, %s)", name.c_str(),
                     scheme->str().c_str()));
  state[name] = scheme;
}

types::SchemeRef SchemeResolver::lookup_scheme(
    const Identifier &id,
    std::set<Identifier> &candidates) const {
  auto iter = state.find(id.name);
  if (iter != state.end()) {
    return iter->second;
  } else {
    std::string upper_name = to_upper(id.name);
    for (auto &pair : state) {
      /* look for a substring match in another symbol */
      std::string regex = "[^.]+\\.?" + regex_sanitize(upper_name);
      if (regex_match(to_upper(pair.first), regex)) {
        candidates.insert(Identifier{pair.first, pair.second->get_location()});
      }
    }
    if (parent != nullptr) {
      return parent->lookup_scheme(id, candidates);
    } else {
      auto user_error = zion::user_error(
          id.location, "symbol " c_id("%s") " is undefined", id.name.c_str());
      for (auto &id : candidates) {
        user_error.add_info(id.location, "did you mean %s?", id.str().c_str());
      }
      throw user_error;
    }
  }
}

void SchemeResolver::rebind(const types::Map &bindings) const {
  if (parent != nullptr) {
    parent->rebind(bindings);
  }

  types::Scheme::Map new_state;
  for (const auto &pair : state) {
    const auto &name = pair.first;
    const auto &scheme = pair.second;
    assert(scheme != nullptr);
    if (!set_intersect(scheme->ftvs(), set_keys(bindings)).empty()) {
      log("there is an intersection on %s between %s and %s", name.c_str(),
          scheme->str().c_str(), ::str(bindings).c_str());
      dbg();
    }
#if 0
    auto new_scheme = scheme->rebind(bindings);
    log("SchemeResolver::rebind(...): rebinding %s to %s",
        scheme->str().c_str(), new_scheme->str().c_str());

    new_state[name] = new_scheme;
#endif
  }
  // std::swap(state, new_state);
}

std::string SchemeResolver::str() const {
  std::stringstream ss;
  ss << "{";
  const char *delim = "";
  for (auto &pair : state) {
    ss << delim;
    delim = ", ";
    ss << pair.first << ": " << pair.second->str();
  }
  if (parent != nullptr) {
    ss << delim << "parent: " << parent->str();
    delim = ", ";
  }
  ss << "}";
  return ss.str();
}

} // namespace types
