#include "solver.h"

#ifdef ZION_DEBUG
#include "ast.h"
#endif

#include "colors.h"
#include "dbg.h"
#include "unification.h"
#include "user_error.h"

namespace zion {

namespace {

#ifdef ZION_DEBUG
void check_constraints_cover_tracked_types(
    const Context &context,
    const TrackedTypes &tracked_types,
    const types::Constraints &constraints) {
  types::Ftvs ftvs;
  for (auto pair : tracked_types) {
    const types::Ftvs &s = pair.second->get_ftvs();
    set_concat(ftvs, s);
    debug_above(5, log_location(pair.first->get_location(),
                                "%s :: %s contains {%s}",
                                pair.first->str().c_str(),
                                pair.second->str().c_str(), join(s).c_str()));
  }

  types::Ftvs constrained_tvs;
  for (auto &constraint : constraints) {
    set_concat(constrained_tvs, constraint.a->get_ftvs());
    set_concat(constrained_tvs, constraint.b->get_ftvs());
  }
  for (auto &constrained_tv : constrained_tvs) {
    ftvs.erase(constrained_tv);
  }
  if (ftvs.size() != 0) {
    log("not all ftvs in tracked types are constrained {%s}",
        join(ftvs, ", ").c_str());
    dbg();
  }
}
#endif

} // namespace

types::Map solver(bool check_constraint_coverage,
                  Context &&context,
                  types::Constraints &constraints,
                  TrackedTypes &tracked_types,
                  const types::SchemeResolver &scheme_resolver,
                  types::ClassPredicates &instance_requirements) {
  debug_above(2, log("solver(%s, ... %d constraints)", context.message.c_str(),
                     constraints.size()));
#ifdef ZION_DEBUG
  if (debug_level() > 3) {
    for (auto &constraint : constraints) {
      log_location(best_location(constraint.a->get_location(),
                                 constraint.b->get_location()),
                   "%s", constraint.str().c_str());
    }
  }
  if (check_constraint_coverage) {
    check_constraints_cover_tracked_types(context, tracked_types, constraints);
  }
#endif

  types::Map bindings;
  std::list<std::pair<Context, types::Unification>> errors;

  for (auto iter = constraints.begin(); iter != constraints.end();) {
    types::Unification unification = types::unify(iter->a, iter->b);
    if (unification.result) {
      if (unification.bindings.size() != 0) {
        rebind_tracked_types(tracked_types, unification.bindings);
        scheme_resolver.rebind(unification.bindings);

        /* save the bindings */
        types::Map new_bindings = compose(unification.bindings, bindings);
        std::swap(bindings, new_bindings);
      }
      ++iter;

      rebind_constraints(iter, constraints.end(), unification.bindings);

      /* rebind the class predicates */
      instance_requirements = types::rebind(instance_requirements,
                                            unification.bindings);
      continue;
    } else {
      errors.push_back({iter->context, unification});
      ++iter;
    }
  }
  if (errors.size() != 0) {
    auto iter = errors.begin();
    auto _error = user_error(iter->first.location, "while checking that %s",
                             iter->first.message.c_str());
    _error.add_info(iter->second.error_location, c_error("error:") " %s",
                    iter->second.error_string.c_str());
    for (++iter; iter != errors.end(); ++iter) {
      _error.add_info(iter->first.location,
                      c_error("error:") " while checking that %s",
                      iter->first.message.c_str());
      _error.add_info(iter->second.error_location, c_error("error:") " %s",
                      iter->second.error_string.c_str());
    }
    throw _error;
  }
  return bindings;
}

} // namespace zion
