#include "class_predicate.h"

#include <cstring>
#include <ctype.h>
#include <sstream>

#include "colors.h"
#include "dbg.h"
#include "ptr.h"
#include "types.h"
#include "unification.h"
#include "utils.h"

namespace types {

size_t ClassPredicateRefHasher::operator()(const ClassPredicateRef &rhs) const {
  return std::hash<std::string>()(rhs->repr());
}

bool ClassPredicateRefEqualTo::operator()(const ClassPredicateRef &lhs,
                                          const ClassPredicateRef &rhs) const {
  return *lhs == *rhs;
}

ClassPredicate::ClassPredicate(Identifier classname, const types::Refs &params)
    : classname(classname), params(params) {
#ifdef ZION_DEBUG
  if (std::strchr(classname.name.c_str(), '.') != nullptr) {
    assert(isupper(split(classname.name, ".")[1][0]));
  } else {
    assert(isupper(classname.name[0]));
  }
#endif
}

ClassPredicate::ClassPredicate(Identifier classname, const Identifiers &params)
    : ClassPredicate(classname, type_variables(params)) {
}

Location ClassPredicate::get_location() const {
  return classname.location;
}

bool ClassPredicate::operator==(const ClassPredicate &rhs) const {
  if (classname.name != rhs.classname.name) {
    return false;
  }

  if (params.size() != rhs.params.size()) {
    return false;
  }

  for (size_t i = 0; i < params.size(); ++i) {
    if (!type_equality(params[i], rhs.params[i])) {
      return false;
    }
  }
  return true;
}

std::string ClassPredicate::repr() const {
  if (!has_repr_) {
    std::stringstream ss;
    ss << classname.name;
    for (auto &param : params) {
      if (dyncast<const types::TypeOperator>(param)) {
        ss << " (" << param->repr() << ")";
      } else {
        ss << " " << param->repr();
      }
    }
    repr_ = ss.str();
  }

  return repr_;
}

std::string ClassPredicate::str() const {
  std::stringstream ss;
  ss << C_TYPECLASS << classname.name << C_RESET;
  for (auto &param : params) {
    if (dyncast<const types::TypeOperator>(param)) {
      ss << " (" << param->str() << ")";
    } else {
      ss << " " << param->str();
    }
  }
  return ss.str();
}

ClassPredicates rebind(const ClassPredicates &class_predicates,
                       const Map &bindings) {
  if (class_predicates.size() == 0) {
    return {};
  }
  ClassPredicates new_class_predicates;
  for (auto &cp : class_predicates) {
    new_class_predicates.insert(cp->rebind(bindings));
  }
  debug_above(6, log("rebinding {%s} with bindings %s results in %s",
                     ::str(class_predicates).c_str(), ::str(bindings).c_str(),
                     ::str(new_class_predicates).c_str()));
  return new_class_predicates;
}

ClassPredicate::Ref ClassPredicate::remap_vars(
    const std::map<std::string, std::string> &remapping) const {
  /* remap all the type vars in a ClassPredicate. This is basically just an
   * optimization over rebind. */
  Refs new_params;
  new_params.reserve(params.size());

  for (const types::Ref &param : params) {
    new_params.push_back(param->remap_vars(remapping));
  }

  return std::make_shared<types::ClassPredicate>(classname, new_params);
}

ClassPredicate::Ref ClassPredicate::rebind(const types::Map &bindings) const {
  if (bindings.size() == 0) {
    return shared_from_this();
  }

  /* rebind all the types in a ClassPredicate. */
  Refs new_params;
  new_params.reserve(params.size());

  for (const types::Ref &param : params) {
    new_params.push_back(param->rebind(bindings));
  }

  return std::make_shared<types::ClassPredicate>(classname, new_params);
}

const Ftvs &ClassPredicate::get_ftvs() const {
  if (!has_ftvs_) {
    for (auto &param : params) {
      set_merge(ftvs_, param->get_ftvs());
    }
    has_ftvs_ = true;
  }

  return ftvs_;
}

ClassPredicates remap_vars(
    const ClassPredicates &class_predicates,
    const std::map<std::string, std::string> &remapping) {
  /* remap all the type variables referenced in a set of ClassPredicates */
  ClassPredicates new_class_predicates;

  for (const ClassPredicateRef &cp : class_predicates) {
    new_class_predicates.insert(cp->remap_vars(remapping));
  }
  return new_class_predicates;
}

} // namespace types

std::string str(const types::ClassPredicates &pm) {
  bool saw_predicate = false;
  std::stringstream ss;
  const char *delim = "[";
  for (auto &class_predicate : pm) {
    ss << delim;
    ss << class_predicate->str();
    delim = ", ";
    saw_predicate = true;
  }
  if (saw_predicate) {
    ss << "]";
  }

  return ss.str();
}
