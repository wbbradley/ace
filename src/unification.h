#pragma once
#include <map>
#include <memory>
#include <vector>

#include "env.h"
#include "infer.h"
#include "ptr.h"
#include "types.h"

/* Unification is basically just an Either result type. */
struct Unification {
  bool result;
  Location error_location;
  std::string error_string;
  types::Map bindings;
};

types::Map solver(bool check_constraint_coverage,
                  Context &&context,
                  Constraints &constraints,
                  Env &env);
Unification unify(types::Ref a, types::Ref b);
Unification unify_many(const types::Refs &as, const types::Refs &b);
types::Map compose(const types::Map &a, const types::Map &b);
Unification compose(const Unification &a, const Unification &b);
bool type_equality(types::Ref a, types::Ref b);
bool scheme_equality(types::Scheme::Ref a, types::Scheme::Ref b);
void rebind_constraints(Constraints::iterator iter,
                        const Constraints::iterator &end,
                        const types::Map &env);
