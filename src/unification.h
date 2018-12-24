#pragma once
#include "ptr.h"
#include <memory>
#include <vector>
#include <map>
#include "types.h"
#include "infer.h"

struct env_t;

struct unification_t {
	types::type_t::map bindings;
	types::type_t::refs instances;
};

unification_t solver(const unification_t &unification, const constraints_t &constraints, env_t &env);
unification_t unify(types::type_t::ref a, types::type_t::ref b);
unification_t unify_many(std::vector<types::type_t::ref> as, std::vector<types::type_t::ref> b);
unification_t compose(const unification_t &a, const unification_t &b);
bool type_equality(types::type_t::ref a, types::type_t::ref b);
constraints_t rebind_constraints(const constraints_t &constraints, const types::type_t::map &env);

