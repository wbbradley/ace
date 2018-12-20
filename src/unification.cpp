#include "zion.h"
#include "dbg.h"
#include "logger.h"
#include <sstream>
#include "utils.h"
#include "types.h"
#include "unification.h"
#include "env.h"
#include "user_error.h"

using namespace types;

bool type_equality(types::type_t::ref a, types::type_t::ref b) {
	if (auto ti_a = dyncast<const type_id_t>(a)) {
		if (auto ti_b = dyncast<const type_id_t>(b)) {
			return ti_a->id.name == ti_b->id.name;
		} else {
			return false;
		}
	} else if (auto tv_a = dyncast<const type_variable_t>(a)) {
		if (auto tv_b = dyncast<const type_variable_t>(b)) {
			return tv_a->id.name == tv_b->id.name;
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
	return in(a, type->get_ftvs());
}

type_t::map bind(std::string a, type_t::ref type) {
    if (auto tv = dyncast<const type_variable_t>(type)) {
	   	if (tv->id.name == a) {
			return {};
		}
    }
    if (occurs_check(a, type)) {
        throw user_error(type->get_location(), "infinite type detected! %s = %s", a.c_str(), type->str().c_str());
    }
    debug_above(6, log("binding type variable %s to %s", a.c_str(), type->str().c_str()));
	type_t::map s;
    s[a] = type;
    return s;
}

type_t::map unify(type_t::ref a, type_t::ref b) {
	debug_above(8, log("unify(%s, %s)", a->str().c_str(), b->str().c_str()));
	if (type_equality(a, b)) {
		return {};
	}

	if (auto tv_a = dyncast<const type_variable_t>(a)) {
		return bind(tv_a->id.name, b);
	} else if (auto tv_b = dyncast<const type_variable_t>(b)) {
		return bind(tv_b->id.name, a);
	} else if (auto to_a = dyncast<const type_operator_t>(a)) {
		if (auto to_b = dyncast<const type_operator_t>(b)) {
			return unify_many(
					{to_a->oper, to_a->operand}, 
					{to_b->oper, to_b->operand});
		}
	} else if (auto tup_a = dyncast<const type_tuple_t>(a)) {
		if (auto tup_b = dyncast<const type_tuple_t>(b)) {
			return unify_many(tup_a->dimensions, tup_b->dimensions);
		}
	}

	auto error = user_error(a->get_location(), "typing error");
	error.add_info(b->get_location(), "types do not unify. %s != %s",
			a->str().c_str(),
			b->str().c_str());
	throw error;
}

types::type_t::map compose(types::type_t::map a, types::type_t::map b) {
	types::type_t::map m;
    for (auto pair : b) {
        m[pair.first] = pair.second->rebind(a);
    }
    for (auto pair : a) {
        m[pair.first] = pair.second;
    }
    return m;
}

std::vector<type_t::ref> rebind_tails(const std::vector<type_t::ref> &types, const type_t::map &env) {
	assert(1 <= types.size());
	std::vector<type_t::ref> new_types;
	for (int i=1; i<types.size(); ++i) {
		new_types.push_back(types[i]->rebind(env));
	}
	return new_types;
}

types::type_t::map unify_many(std::vector<types::type_t::ref> as, std::vector<types::type_t::ref> bs) {
    debug_above(8, log("unify_many([%s], [%s])", join_str(as, ", ").c_str(), join_str(bs, ", ").c_str()));
    if (as.size() == 0 && bs.size() == 0) {
        return {};
    } else if (as.size() != bs.size()) {
        throw user_error(as[0]->get_location(), "unification mismatch %s != %s",
			   	join_str(as, " -> ").c_str(), join(bs, " -> ").c_str());
    }

    auto su1 = unify(as[0], bs[0]);
    auto su2 = unify_many(rebind_tails(as, su1), rebind_tails(bs, su1));
    return compose(su2, su1);
}
