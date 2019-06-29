#pragma once
#include <map>
#include <memory>
#include <vector>

#include "env.h"
#include "infer.h"
#include "ptr.h"
#include "types.h"

struct Unification {
  bool result;
  Location error_location;
  std::string error_string;
  types::Type::map bindings;
  std::vector<InstanceRequirement> instance_requirements;

  void add_instance_requirement(const InstanceRequirement &ir);
};

types::Type::map solver(bool check_constraint_coverage,
                        Context &&context,
                        constraints_t &constraints,
                        Env &env);
Unification unify(types::Type::ref a, types::Type::ref b);
Unification unify_many(const types::Type::refs &as, const types::Type::refs &b);
types::Type::map compose(const types::Type::map &a, const types::Type::map &b);
Unification compose(const Unification &a, const Unification &b);
bool type_equality(types::Type::ref a, types::Type::ref b);
bool scheme_equality(types::Scheme::ref a, types::Scheme::ref b);
void rebind_constraints(constraints_t::iterator iter,
                        const constraints_t::iterator &end,
                        const types::Type::map &env);
