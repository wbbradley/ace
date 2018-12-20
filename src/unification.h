#pragma once
#include "ptr.h"
#include <memory>
#include <vector>
#include <map>
#include "types.h"
#include "infer.h"

struct env_t;

types::type_t::map solver(const types::type_t::map &subst, const constraints_t &constraints, env_t &env);
types::type_t::map unify(types::type_t::ref a, types::type_t::ref b);
types::type_t::map unify_many(std::vector<types::type_t::ref> as, std::vector<types::type_t::ref> b);
types::type_t::map compose(const types::type_t::map &a, const types::type_t::map &b);
bool type_equality(types::type_t::ref a, types::type_t::ref b);
constraints_t rebind_constraints(const constraints_t &constraints, const types::type_t::map &env);
