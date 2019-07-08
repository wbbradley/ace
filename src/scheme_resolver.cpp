#include "scheme_resolver.h"

types::SchemeRef SchemeResolver::resolve(Location location, std::string name) {
  auto iter = state.find(name);
  if (iter == state.end()) {
    state[name] = {};
#if 0
  for (const Decl *decl : decls) {
    /* seed each decl with a type variable to let inference resolve */
    env.extend(decl->id, decl->get_early_scheme(), true /*allow_subscoping*/);
  }
#endif

  
    
  } else {
    if (iter.second == nullptr) {
      throw user_error(location, "inference cycle detected for %s",
                       name.c_str());
    }
    return iter.second;
  }

    map.insert(iter, 

  assert(false);
  return {};
}

void SchemeResolver::rebind(std::string name, const types::Map &bindings) const {
  for (auto pair : map) {
    map[pair.first] = pair.second->rebind(bindings);
  }
}
