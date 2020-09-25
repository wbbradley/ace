#include "scheme.h"

#include "dbg.h"

namespace types {

Scheme::Scheme(Location location,
               const std::vector<std::string> &vars,
               const ClassPredicates &predicates,
               types::Ref type)
    : location(location), vars(vars), predicates(predicates), type(type) {
#ifdef ZION_DEBUG
  if (vars.size() == 0) {
    if (types::get_ftvs(predicates).size() != 0) {
      log("unexpected lack of vars in scheme %s", str().c_str());
      dbg();
    }
  }
#endif
}

types::Ref Scheme::instantiate(Location location) const {
  types::Map subst;
  for (auto var : vars) {
    subst[var] = type_variable(gensym(location));
  };
  return type->rebind(subst)->with_location(location);
}

static Map remove_bindings(const Map &env,
                           const std::vector<std::string> &vars) {
  Map new_map{env};
  for (auto var : vars) {
    new_map.erase(var);
  }
  return new_map;
}

Scheme::Ref Scheme::rebind(const types::Map &bindings) const {
  /* this is subtle because it actually rebinds type variables that are free
   * within the not-yet-normalized scheme. This is because the map containing
   * the schemes is a working set of types that are waiting to be bound. In some
   * cases the variability of the inner types can be resolved. */
  if (bindings.size() == 0) {
    return shared_from_this();
  }
  return scheme(location, vars, predicates,
                type->rebind(remove_bindings(bindings, vars)));
}

Scheme::Ref Scheme::normalize() const {
  std::map<std::string, std::string> ord;
  std::vector<std::string> new_vars;

  int counter = 0;
  for (auto &ftv : vars) {
    new_vars.push_back(alphabetize(counter++));
    ord[ftv] = new_vars.back();
  }
  return scheme(location, new_vars, types::remap_vars(predicates, ord),
                type->remap_vars(ord));
}

Scheme::Ref Scheme::freshen() const {
  return freshen(location);
}

Scheme::Ref Scheme::freshen(Location location) const {
  if (vars.size() == 0) {
    if (location != get_location()) {
      return scheme(location, {}, predicates, type);
    } else {
      return shared_from_this();
    }
  }

  std::map<std::string, std::string> remapping;
  std::vector<std::string> new_vs;
  for (auto &v : vars) {
    assert(!in(v, remapping));
    new_vs.push_back(gensym_name());
    remapping[v] = new_vs.back();
  }

  return std::make_shared<Scheme>(location, new_vs,
                                  types::remap_vars(predicates, remapping),
                                  type->remap_vars(remapping));
}

Ftvs Scheme::ftvs() const {
  if (!has_ftvs) {
    has_ftvs = true;
    cached_ftvs = type->get_ftvs();
    for (auto &v : vars) {
      cached_ftvs.erase(v);
    }
  }
  return cached_ftvs;
}

std::string Scheme::str() const {
  std::stringstream ss;
  const char *delim = "";
  if (vars.size() != 0) {
    ss << "(forall " << C_TYPE << join(vars, " ") << C_RESET;
    delim = " ";
  }

  auto predicates_str = ::str(predicates);
  if (predicates_str.size() != 0) {
    ss << delim << C_CONTROL "where " C_RESET << predicates_str;
    delim = " ";
  }

  if (vars.size() != 0) {
    ss << delim << ".";
    delim = " ";
  }
  ss << delim << type->str();
  if (vars.size() != 0) {
    ss << ")";
  }
  return ss.str();
}

std::string Scheme::repr() const {
  std::stringstream ss;
  if (vars.size() != 0) {
    // ∀
    ss << "(forall " << join(vars, " ");
    auto predicates_str = ::str(predicates);
    if (predicates_str.size() != 0) {
      ss << " where " << predicates_str;
    }
    ss << ::str(predicates);
    ss << " . ";
  }
  type->emit(ss, {}, 0);
  if (vars.size() != 0) {
    ss << ")";
  }
  return ss.str();
}

int Scheme::btvs() const {
  /* get the number of type variables that are predicated */
  const Ftvs &ftvs = type->get_ftvs();
  Ftvs predicated_tvs;
  for (auto &cp : predicates) {
    set_merge(predicated_tvs, cp->get_ftvs());
  }
  return set_intersect(ftvs, predicated_tvs).size();
}

Location Scheme::get_location() const {
  return location;
}

} // namespace types

std::string str(const types::Scheme::Map &m) {
  std::stringstream ss;
  ss << "{";
  ss << join_with(m, ", ", [](const auto &pair) {
    return string_format("%s: %s", pair.first.c_str(),
                         pair.second->str().c_str());
  });
  ss << "}";
  return ss.str();
}

types::Scheme::Ref scheme(Location location,
                          std::vector<std::string> vars,
                          const types::ClassPredicates &predicates,
                          const types::Ref &type) {
  return std::make_shared<types::Scheme>(location, vars, predicates, type);
}
