#pragma once
#include <map>
#include <memory>
#include <vector>

#include "env.h"
#include "infer.h"
#include "ptr.h"
#include "types.h"

struct unification_t {
    bool result;
    location_t error_location;
    std::string error_string;
    types::type_t::map bindings;
    std::vector<instance_requirement_t> instance_requirements;

    void add_instance_requirement(const instance_requirement_t &ir);
};

types::type_t::map solver(const types::type_t::map &bindings,
                          const constraints_t &constraints,
                          env_t &env);
unification_t unify(types::type_t::ref a, types::type_t::ref b);
unification_t unify_many(const types::type_t::refs &as, const types::type_t::refs &b);
types::type_t::map compose(const types::type_t::map &a, const types::type_t::map &b);
unification_t compose(const unification_t &a, const unification_t &b);
bool type_equality(types::type_t::ref a, types::type_t::ref b);
bool scheme_equality(types::scheme_t::ref a, types::scheme_t::ref b);
constraints_t rebind_constraints(const constraints_t &constraints,
                                 const types::type_t::map &env);
