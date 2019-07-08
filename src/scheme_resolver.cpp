#include "scheme_resolver.h"

#include "user_error.h"

namespace zion {

types::SchemeRef SchemeResolver::resolve(Location location, std::string name) {
  auto iter = state.find(name);
  if (iter == state.end()) {
    state[name] = {};
    if (map.count(name) == 0) {
      throw user_error(location, "symbol " c_id("%s") " does not exist",
                       name.c_str());
    } else {
      /* call the resolver for |name| */
      types::SchemeRef scheme = (*map.at(name))();
      assert(state.at(name) == nullptr);
      state[name] = scheme;

      log_location(location, "SchemeResolver::resolve(..., %s) -> %s",
                   name.c_str(), scheme->str().c_str());
      return scheme;
    }
#if 0
  for (const Decl *decl : decls) {
    /* seed each decl with a type variable to let inference resolve */
    env.extend(decl->id, decl->get_early_scheme(), true /*allow_subscoping*/);
  }
#endif
  } else {
    if (iter->second == nullptr) {
      throw user_error(location, "inference cycle detected for %s",
                       name.c_str());
    }
    return iter->second;
  }
}

void SchemeResolver::precache(std::string name,
                              const types::SchemeRef &scheme) {
  assert(state.at(name) == nullptr);
  log("precache state in SchemeResolver. %s :: %s", name.c_str(),
      scheme->str().c_str());
  state[name] = scheme;
}

void SchemeResolver::rebind(const types::Map &bindings) {
  for (auto pair : map) {
    auto scheme = get(state, pair.first, types::SchemeRef{});
    if (scheme != nullptr) {
      auto new_scheme = scheme->rebind(bindings);
      log("SchemeResolver::rebind(...): rebinding %s to %s",
          scheme->str().c_str(), new_scheme->str().c_str());

      state[pair.first] = new_scheme;
    }
  }
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
  ss << "}";
  return ss.str();
}

} // namespace zion
