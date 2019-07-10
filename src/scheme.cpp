#include "scheme.h"

#include "dbg.h"

namespace types {

Scheme::Scheme(const std::vector<std::string> &vars,
               const ClassPredicates &predicates,
               types::Ref type)
    : vars(vars), predicates(predicates), type(type) {
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
  if (!type->get_ftvs().empty()) {
    log("trying to instantiate %s", str().c_str());
    assert(false);
  }
  return type;
#if 0
  types::Map subst;
  for (auto var : vars) {
    subst[var] = type_variable(gensym(location));
  };
  return type->rebind(subst);
#endif
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
  return scheme(vars, predicates,
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
  return scheme(new_vars, types::remap_vars(predicates, ord),
                type->remap_vars(ord));
}

Scheme::Ref Scheme::freshen() const {
  if (vars.size() == 0) {
    return shared_from_this();
  }

  std::map<std::string, std::string> remapping;
  std::vector<std::string> new_vs;
  for (auto &v : vars) {
    assert(!in(v, remapping));
    new_vs.push_back(gensym_name());
    remapping[v] = new_vs.back();
  }

  return std::make_shared<Scheme>(new_vs,
                                  types::remap_vars(predicates, remapping),
                                  type->remap_vars(remapping));
}

Ftvs Scheme::ftvs() const {
  auto ftvs = type->get_ftvs();
  for (auto &v : vars) {
    ftvs.erase(v);
  }
  return ftvs;
}

std::string Scheme::str() const {
  std::stringstream ss;
  const char *delim = "";
  if (vars.size() != 0) {
    ss << "(∀ " << C_TYPE << join(vars, " ") << C_RESET;
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
    ss << "(∀ " << join(vars, " ");
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
  return type->get_location();
}

} // namespace types
