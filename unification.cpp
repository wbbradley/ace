#include "zion.h"
#include "dbg.h"
#include "logger.h"
#include <sstream>
#include "utils.h"
#include "types.h"
#include "unification.h"

unification_t::unification_t(
		bool result,
		std::string reasons,
		types::type::map bindings) :
	result(result),
	reasons(reasons),
	bindings(bindings)
{
	debug_above(10, log(log_info, "unification result {%s, %s, %s}",
				result ? "success" : "failure", reasons.c_str(),
				::str(bindings).c_str()));
}

types::type::ref prune(types::type::ref t, types::type::map bindings) {
	/* Follow the links across the bindings to reach the final binding. */
	atom type_variable_name;
	if (get_type_variable_name(t, type_variable_name)) {
        if (bindings.find(type_variable_name) != bindings.end()) {
            return prune(bindings[type_variable_name], bindings);
		}
	}

	return t;
}

template <typename T>
bool occurs_in(const ptr<types::type_variable> &var, const T &terms) {
    /* checks whether a term variable occurs in any other terms. */
	for (auto &term : terms) {
		if (occurs_in_type(var, term)) {
			return true;
		}
	}
	return false;
}

bool occurs_in_type(
		types::type_variable::ref var,
	   	types::type::ref b,
	   	types::type::map bindings)
{
	/* checks whether a type variable occurs in a type expression. must be
	 * called with var already pruned */
	assert(var == prune(var, bindings));
    auto pruned_b = prune(b, bindings);

	if (auto var2 = dyncast<const types::type_variable>(pruned_b)) {
		return var2 == var;
	} else if (auto type_operator = dyncast<const types::type_operator>(pruned_b)) {
		return occurs_in_type(var, type_operator->oper, bindings) ||
			occurs_in_type(var, type_operator->operand, bindings);
	} else {
		// TODO: handle type_product, type_sum
		return false;
	}
}


unification_t unify_core(
		types::type::ref lhs,
		types::type::ref rhs,
		types::term::map env,
		types::type::map bindings,
		int depth)
{
	if (depth > 12) {
		log(log_error, "unification depth is getting big...");
		dbg();
	}

	assert(lhs != nullptr);
	assert(rhs != nullptr);

	debug_above(7, log(log_info, "unify_core(%s, %s, %s, %s)",
			   	lhs->str().c_str(),
			   	rhs->str().c_str(),
				str(env).c_str(),
				str(bindings).c_str()));

    auto a = prune(lhs, bindings);
    auto b = prune(rhs, bindings);

    if (a->str(bindings) == b->str(bindings)) {
		debug_above(7, log(log_info, "matched " c_type("%s"), a->str(bindings).c_str()));
        return {true, "", bindings};
	}

	auto pto_a = dyncast<const types::type_operator>(a);
	auto pto_b = dyncast<const types::type_operator>(b);

	auto pts_a = dyncast<const types::type_sum>(a);
	auto pts_b = dyncast<const types::type_sum>(b);

	auto ptp_a = dyncast<const types::type_product>(a);

	if (auto ptv = dyncast<const types::type_variable>(a)) {
		if (a != b) {
			if (occurs_in_type(ptv, b, bindings)) {
				return {
					false,
					string_format("recursive unification on %s and %s",
							a->str().c_str(), b->str().c_str()),
					bindings};
			}
			debug_above(8, log(log_info, "binding " c_id("%s") " to " c_type("%s"),
						ptv->id->get_name().c_str(),
						b->str(bindings).c_str()));
			assert(bindings.find(ptv->id->get_name()) == bindings.end());
			if (b->rebind(bindings)->ftv() != 0) {
				debug_above(5, log(log_warning, "note that %s is itself not fully bound", b->str().c_str()));
			}
			bindings[ptv->id->get_name()] = b;
		} else {
			assert(false);
		}

		return {true, "", bindings};
	} else if (auto ptv_b = dyncast<const types::type_variable>(b)) {
		if (a != b) {
			if (occurs_in_type(ptv_b, a, bindings)) {
				return {
					false,
					string_format("recursive unification on %s and %s",
							a->str().c_str(), b->str().c_str()),
					bindings};
			}
			debug_above(8, log(log_info, "binding " c_id("%s") " to " c_type("%s"),
						ptv_b->id->get_name().c_str(),
						a->str(bindings).c_str()));
			assert(bindings.find(ptv_b->id->get_name()) == bindings.end());
			if (a->rebind(bindings)->ftv() != 0) {
				debug_above(5, log(log_warning, "note that %s is itself not fully bound", a->str().c_str()));
			}
			bindings[ptv_b->id->get_name()] = a;
		} else {
			assert(false);
		}

		return {true, "", bindings};
	} else if (pts_a != nullptr) {
		if (pts_b == nullptr) {
			std::vector<std::string> reasons;
			for (auto option : pts_a->options) {
				auto unification = unify_core(option, b, env, bindings, depth + 1);
				if (unification.result) {
					debug_above(2, log(log_info, "replacing bindings %s with %s",
								str(bindings).c_str(),
								str(unification.bindings).c_str()));
					bindings = unification.bindings;
					return {true, option->str(bindings), bindings};
				} else {
					reasons.push_back(unification.reasons);
				}
			}
			return {false, join(reasons, "\n\t"), {}};
		} else {
			assert(pts_b != nullptr);
			for (auto inbound_option : pts_b->options) {
				debug_above(8, log(log_info, "checking inbound %s against %s",
							inbound_option->repr().c_str(), a->repr().c_str()));
				auto unification = unify_core(a, inbound_option, env, bindings, depth + 1);
				if (unification.result) {
					bindings = unification.bindings;
				} else {
					return {false, string_format(
							"\n\tcould not find a match for \n\t\t%s"
							"\n\tin\n\t\t%s",
							inbound_option->str(bindings).c_str(),
							a->str(bindings).c_str()), bindings};
				}
			}
			return {true, "inbound type is a subset of outbound type", bindings};
		}
	} else if (ptp_a != nullptr) {
		if (auto ptp_b = dyncast<const types::type_product>(b)) {
			if (ptp_a->dimensions.size() != ptp_b->dimensions.size()) {
				return {false, string_format("product type lengths do not match "
						"(a = %s, b = %s)", ptp_a->str().c_str(),
						ptp_b->str().c_str()), bindings};
			} else {
				auto a_dims_end = ptp_a->dimensions.end();
				auto b_dims_iter = ptp_b->dimensions.begin();
				for (auto a_dims_iter = ptp_a->dimensions.begin();
						a_dims_iter != a_dims_end;
						++a_dims_iter, ++b_dims_iter) {
					auto unification = unify_core(*a_dims_iter, *b_dims_iter,
							env, bindings, depth + 1);
					if (!unification.result) {
						return {false, unification.reasons, {}};
					}
					bindings = unification.bindings;
				}

				return {true, "products match", bindings};
			}
		} else {
			return {false, "inbound type is not a product type", bindings};
		}
	} else if (pto_a != nullptr && pto_b != nullptr) {
		auto unification = unify_core(pto_a->oper, pto_b->oper, env, bindings, depth + 1);
		if (!unification.result) {
			return {false, unification.reasons, {}};
		}
		bindings = unification.bindings;

		if ((pto_a->operand == nullptr) != (pto_b->operand == nullptr)) {
			return {
				false,
				string_format("type mismatch: %s != %s",
						a->str(bindings).c_str(), b->str(bindings).c_str()),
				{}};
		}

		assert(pto_a->operand != nullptr && pto_b->operand != nullptr);

		return unify_core(pto_a->operand, pto_b->operand, env, bindings, depth + 1);
	} else {
		/* types don't match */
		return {false, string_format("%s <> %s",
				a->str(bindings).c_str(), b->str(bindings).c_str()), {}};
	}
}

unification_t unify(
		status_t &status,
		types::term::ref lhs,
		types::term::ref rhs,
		types::term::map env)
{
	indent_logger indent(2, string_format(
				"unify(" c_term("%s") ", " c_term("%s") ", %s)",
				lhs->str().c_str(), rhs->str().c_str(), str(env).c_str()));

	auto lhs_type = lhs->get_type(status);
	auto rhs_type = rhs->get_type(status);
	unification_t unification = unify_core(
			lhs_type, rhs_type,
			env,
			{}, 0 /*depth*/);

	if (unification.result) {
		return unification;
	} else {
		debug_above(10, log(log_info, "straight unification did not work"));

		/* straight unification did not work, let's try evaluating the types
		 * to see whether they will unify after substitution */
		auto lhs_type = lhs->evaluate(env)->get_type(status);
		auto rhs_type = rhs->evaluate(env)->get_type(status);
		if (!!status) {
			unification_t unification = unify_core(
					lhs_type, rhs_type,
					env,
					{}, 0 /*depth*/);
			assert(!!status);
			return unification;
		} else {
			return {false, "error during unification", {}};
		}
	}
}

