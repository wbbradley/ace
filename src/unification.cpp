#include "zion.h"
#include "dbg.h"
#include <iostream>
#include "logger.h"
#include <sstream>
#include "utils.h"
#include "types.h"
#include "unification.h"
#include "env.h"
#include "user_error.h"

using namespace types;

bool scheme_equality(types::scheme_t::ref a, types::scheme_t::ref b) {
	if (a == nullptr || b == nullptr) {
		assert(false);
		return false;
	}
	// log("checking %s == %s", a->str().c_str(), b->str().c_str());
	// log("normalized checking %s == %s", a->normalize()->str().c_str(), b->normalize()->str().c_str());
	if (a->normalize()->str() == b->normalize()->str()) {
		return true;
	}

	auto ta = a->instantiate(INTERNAL_LOC());
	auto tb = b->instantiate(INTERNAL_LOC());
	auto unification = unify(ta, tb);
	if (!unification.result) {
		debug_above(3, log_location(
				unification.error_location,
				"schemes %s and %s do not match because %s",
				ta->str().c_str(), tb->str().c_str(), unification.error_string.c_str()));
	}
	return unification.result;
}

bool type_equality(types::type_t::ref a, types::type_t::ref b) {
	if (a == b) {
		/* the same pointer */
		return true;
	}

	if (auto ti_a = dyncast<const type_id_t>(a)) {
		if (auto ti_b = dyncast<const type_id_t>(b)) {
			return ti_a->id.name == ti_b->id.name;
		} else {
			return false;
		}
	} else if (auto tv_a = dyncast<const type_variable_t>(a)) {
		if (auto tv_b = dyncast<const type_variable_t>(b)) {
			return tv_a->id.name == tv_b->id.name && tv_a->predicates == tv_b->predicates;
		} else {
			return false;
		}
	} else if (auto to_a = dyncast<const type_operator_t>(a)) {
		if (auto to_b = dyncast<const type_operator_t>(b)) {
			return (
					type_equality(to_a->oper, to_b->oper) &&
					type_equality(to_a->operand, to_b->operand));
		} else {
			return false;
		}
	} else if (auto tp_a = dyncast<const type_ptr_t>(a)) {
		if (auto tp_b = dyncast<const type_ptr_t>(b)) {
			return type_equality(tp_a->element_type, tp_b->element_type);
		} else {
			return false;
		}
	} else if (auto tup_a = dyncast<const type_tuple_t>(a)) {
		if (auto tup_b = dyncast<const type_tuple_t>(b)) {
			if (tup_a->dimensions.size() != tup_b->dimensions.size()) {
				return false;
			}
			for (int i=0; i<tup_a->dimensions.size(); ++i) {
				if (!type_equality(tup_a->dimensions[i],tup_b->dimensions[i])) {
					return false;
				}
			}
			return true;
		}
	} else {
		auto error = user_error(a->get_location(), "type_equality is not implemented between these two types");
		error.add_info(b->get_location(), "%s and %s", a->str().c_str(), b->str().c_str());
		throw error;
	}
	return false;
}

bool occurs_check(std::string a, type_t::ref type) {
	return in(a, type->get_predicate_map());
}

unification_t bind(std::string a, type_t::ref type, const std::set<std::string> &instances) {
    if (occurs_check(a, type)) {
		return unification_t{false, type->get_location(), string_format("infinite type detected! %s = %s", a.c_str(), type->str().c_str()), {}, {}};
    }

	unification_t unification{true, INTERNAL_LOC(), "", {}, {}};
	if (auto tv = dyncast<const types::type_variable_t>(type)) {
		if (tv->id.name == a && all_in(instances, tv->predicates)) {
			assert(false);
			assert(instances.size() == tv->predicates.size());
			return {true, INTERNAL_LOC(), "", {}, {}};
		}

		type = type_variable(gensym(type->get_location()), set_union(instances, tv->predicates));
		debug_above(10, log("adding a binding from %s to new freshie %s", tv->id.str().c_str(), type->str().c_str()));
		unification.bindings[tv->id.name] = type;
	} else {
		if (instances.size() != 0) {
			for (auto instance : instances) {
				unification.add_instance_requirement({instance, type->get_location(), type});
			}
		}
	}

    unification.bindings[a] = type;
    debug_above(6, log("binding type variable %s to %s gives bindings %s", a.c_str(), type->str().c_str(),
			   	str(unification.bindings).c_str()));
    return unification;
}

unification_t unify(type_t::ref a, type_t::ref b) {
	debug_above(8, log("unify(%s, %s)", a->str().c_str(), b->str().c_str()));
	if (type_equality(a, b)) {
		return unification_t{true, INTERNAL_LOC(), "", {}, {}};
	}

	if (auto tv_a = dyncast<const type_variable_t>(a)) {
		return bind(tv_a->id.name, b, tv_a->predicates);
	} else if (auto tv_b = dyncast<const type_variable_t>(b)) {
		return bind(tv_b->id.name, a, tv_b->predicates);
	} else if (auto to_a = dyncast<const type_operator_t>(a)) {
		if (auto to_b = dyncast<const type_operator_t>(b)) {
			return unify_many(
					{to_a->oper, to_a->operand}, 
					{to_b->oper, to_b->operand});
		}
	} else if (auto tp_a = dyncast<const type_ptr_t>(a)) {
		if (auto tp_b = dyncast<const type_ptr_t>(b)) {
			return unify(tp_a->element_type, tp_b->element_type);
		}
	} else if (auto tup_a = dyncast<const type_tuple_t>(a)) {
		if (auto tup_b = dyncast<const type_tuple_t>(b)) {
			return unify_many(tup_a->dimensions, tup_b->dimensions);
		}
	}

	auto location = best_location(a->get_location(), b->get_location());
	return unification_t{false, location, string_format("type error. %s != %s",
			a->str().c_str(),
			b->str().c_str()), {}, {}};
}

types::type_t::map solver(
		const types::type_t::map &bindings,
		const constraints_t &constraints,
		env_t &env)
{
	if (constraints.size() == 0) {
		return bindings;
	}

	unification_t unification = unify(constraints[0].a, constraints[0].b);
	if (unification.result) {
		auto new_bindings = compose(unification.bindings, bindings);
		for (auto &instance_requirement : unification.instance_requirements) {
			env.add_instance_requirement(instance_requirement);
		}
		env.rebind(new_bindings);
		return solver(new_bindings, rebind_constraints(constraints, new_bindings), env);
	} else {
		auto error = user_error(unification.error_location, "%s", unification.error_string.c_str());
		error.add_info(constraints[0].info.location, "while checking that %s", constraints[0].info.reason.c_str());
		throw error;
	}
}

types::type_t::map compose(const types::type_t::map &a, const types::type_t::map &b) {
	debug_above(11, log("composing {%s} with {%s}",
			join_with(a, ", ", [](const auto &pair) {
				return string_format("%s: %s", pair.first.c_str(), pair.second->str().c_str());
				}).c_str(),
			join_with(b, ", ", [](const auto &pair) {
				return string_format("%s: %s", pair.first.c_str(), pair.second->str().c_str());
				}).c_str()));
	types::type_t::map m;
    for (auto pair : b) {
        m[pair.first] = pair.second->rebind(a);
    }
    for (auto pair : a) {
		debug_above(11, log("-- check %s in %s when going to assign it to %s -- ", pair.first.c_str(), str(m).c_str(),
				pair.second->str().c_str()));
		assert(!in(pair.first, m));
        m[pair.first] = pair.second;
    }
	debug_above(11, log("which gives: %s",
			join_with(m, ", ", [](const auto &pair) {
				return string_format("%s: %s", pair.first.c_str(), pair.second->str().c_str());
				}).c_str()));
    return m;
}

unification_t compose(const unification_t &a, const unification_t &b) {
	if (a.result && b.result) {
		auto unification = unification_t{true, INTERNAL_LOC(), "", compose(a.bindings, b.bindings), {}};
		unification.instance_requirements.reserve(a.instance_requirements.size() + b.instance_requirements.size());

		for (auto ir : a.instance_requirements) {
			unification.add_instance_requirement(ir);
		}
		for (auto ir : b.instance_requirements) {
			unification.add_instance_requirement(ir);
		}
		return unification;
	} else {
		return unification_t{false, a.result ? b.error_location : a.error_location,
			a.result ? b.error_string : a.error_string,
			{}, {}};
	}
}

std::vector<type_t::ref> rebind_tails(const std::vector<type_t::ref> &types, const type_t::map &bindings) {
	assert(1 <= types.size());
	std::vector<type_t::ref> new_types;
	for (int i=1; i<types.size(); ++i) {
		new_types.push_back(types[i]->rebind(bindings));
	}
	return new_types;
}

constraints_t rebind_constraints(const constraints_t &constraints, const type_t::map &bindings) {
	assert(1 <= constraints.size());
	constraints_t new_constraints;
	for (int i=1; i<constraints.size(); ++i) {
		auto &constraint = constraints[i];
		new_constraints.push_back(constraint.rebind(bindings));
	}
	return new_constraints;
}

unification_t unify_many(const types::type_t::refs &as, const types::type_t::refs &bs) {
    debug_above(8, log("unify_many([%s], [%s])", join_str(as, ", ").c_str(), join_str(bs, ", ").c_str()));
    if (as.size() == 0 && bs.size() == 0) {
        return unification_t{true, INTERNAL_LOC(), "", {}, {}};
    } else if (as.size() != bs.size()) {
        throw user_error(as[0]->get_location(), "unification mismatch %s != %s",
			   	join_str(as, " -> ").c_str(), join(bs, " -> ").c_str());
    }

    auto u1 = unify(as[0], bs[0]);
    auto u2 = unify_many(rebind_tails(as, u1.bindings), rebind_tails(bs, u1.bindings));
    return compose(u2, u1);
}

void unification_t::add_instance_requirement(const instance_requirement_t &ir) {
	debug_above(6,
		   	log_location(
				log_info,
				ir.location,
				"adding type class requirement for %s %s",
				ir.type_class_name.c_str(), ir.type->str().c_str()));
	instance_requirements.push_back(ir);
}

