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
bool occurs_in(const ptr<types::type_variable> &var, const T &types) {
    /* checks whether a type variable occurs in any other types. */
	for (auto &type : types) {
		if (occurs_in_type(var, type)) {
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

unification_t unify(
		types::type::ref lhs,
		types::type::ref rhs,
		types::type::map env,
		types::type::map bindings,
		int depth)
{
	if (depth > 12) {
		log(log_error, "unification depth is getting big...");
		dbg();
	}

	assert(lhs != nullptr);
	assert(rhs != nullptr);

	debug_above(7, log(log_info, "unify(%s, %s, ..., %s)",
			   	lhs->str().c_str(),
			   	rhs->str().c_str(),
				str(bindings).c_str()));

    auto a = prune(lhs, bindings);
    auto b = prune(rhs, bindings);

	auto ptm_a = dyncast<const types::type_maybe>(a);
	auto ptm_b = dyncast<const types::type_maybe>(b);

	auto pti_a = dyncast<const types::type_id>(a);
	auto pti_b = dyncast<const types::type_id>(b);

	auto pto_a = dyncast<const types::type_operator>(a);
	auto pto_b = dyncast<const types::type_operator>(b);

	auto pts_a = dyncast<const types::type_sum>(a);
	auto pts_b = dyncast<const types::type_sum>(b);

	auto ptp_a = dyncast<const types::type_product>(a);

	if (pti_a != nullptr) {
		/* check for basic type_id matching */
		if (pti_b != nullptr && pti_a->id->get_name() == pti_b->id->get_name()) {
			/* simple type_id match */
			return {true, "", bindings};
		}
	}

   	if (auto ptv_a = dyncast<const types::type_variable>(a)) {
		if (a != b) {
			if (occurs_in_type(ptv_a, b, bindings)) {
				return {
					false,
					string_format("recursive unification on %s and %s",
							a->str().c_str(), b->str().c_str()),
					bindings};
			}
			debug_above(4, log(log_info, "binding type_variable " c_id("%s") " to " c_type("%s"),
						ptv_a->id->get_name().c_str(),
						b->str(bindings).c_str()));
			assert(bindings.find(ptv_a->id->get_name()) == bindings.end());
			if (b->rebind(bindings)->ftv_count() != 0) {
				debug_above(4, log(log_warning, "note that %s is itself not fully bound", b->str().c_str()));
			}
			bindings[ptv_a->id->get_name()] = b;
		}

		return {true, "", bindings};
	} else if (auto ptv_b = dyncast<const types::type_variable>(b)) {
		return unify(ptv_b, a, env, bindings, depth + 1);
	} else if (ptm_a != nullptr) {
		if (ptm_b != nullptr) {
			return unify(ptm_a->just, ptm_b->just, env, bindings, depth + 1);
		} else if (b->is_nil()) {
			return {true, "", bindings};
		} else {
			return unify(ptm_a->just, b, env, bindings, depth + 1);
		}
	} else if (pti_a != nullptr) {
		/* ok, we've got a mismatch, but we know we have an id on the left-hand
		 * side, let's try expanding the type_id to see whether it will unify
		 * after evaluation. */
		auto new_a = eval_id(pti_a, env);
		if (new_a != nullptr) {
			debug_above(6, log(log_info, "eval_id(%s, env) -> %s",
						pti_a->str().c_str(),
						new_a->str().c_str()));
			return unify(new_a, b, env, bindings, depth + 1);
		} else {
			/* types don't match */
			return {false, string_format("(%s != %s) and (%s could not be expanded further)",
					a->str().c_str(),
					b->str().c_str(),
					a->str().c_str()), {}};
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
					auto unification = unify(*a_dims_iter, *b_dims_iter,
							env, bindings, depth + 1);
					if (!unification.result) {
						return {false, unification.reasons, {}};
					}
					bindings = unification.bindings;
				}

				return {true, "products match", bindings};
			}
		} else {
			return {false, string_format("%s <> %s",
					a->str().c_str(),
					b->str().c_str()), bindings};
		}
	} else if (pts_a != nullptr) {
		if (pts_b == nullptr) {
			std::vector<std::string> reasons;
			for (auto option : pts_a->options) {
				auto unification = unify(option, b, env, bindings, depth + 1);
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
				debug_above(7, log(log_info, "checking inbound %s against %s",
							inbound_option->repr().c_str(), a->repr().c_str()));
				auto unification = unify(a, inbound_option, env, bindings, depth + 1);
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
	} else if (pto_a != nullptr) {
		debug_above(7, log(log_info, "checking inbound type_operator %s",
					pto_a->str().c_str()));
		if (pto_b != nullptr) {
			debug_above(7, log(log_info, "checking outbound type_operator %s",
						pto_b->str().c_str()));
			auto unification = unify(pto_a->oper, pto_b->oper, env, bindings, depth + 1);
			if (unification.result) {
				bindings = unification.bindings;

				if ((pto_a->operand == nullptr) != (pto_b->operand == nullptr)) {
					return {
						false,
							string_format("type mismatch: %s != %s",
									a->str(bindings).c_str(), b->str(bindings).c_str()),
							{}};
				}

				assert(pto_a->operand != nullptr && pto_b->operand != nullptr);

				return unify(pto_a->operand, pto_b->operand, env, bindings, depth + 1);
			}
		} else {
			/* fallthrough, and try expanding the left-hand side */
			debug_above(7, log(log_info, "falling through"));
		}
		auto operator_a = pto_a->oper->rebind(bindings);
		auto operand_a = pto_a->operand->rebind(bindings);

		debug_above(7, log(log_info, "eval_apply(%s, %s, ...)",
					operator_a->str().c_str(), operand_a->str().c_str()));

		/* apply the bindings first, so as to simplify the application */
		auto new_a = eval_apply(operator_a, operand_a, env);

		if (new_a != nullptr) {
			debug_above(7, log(log_info, "eval_apply(%s, %s, ...) -> %s",
						operator_a->str().c_str(), operand_a->str().c_str(),
						new_a->str(bindings).c_str()));

			return unify(new_a, b, env, bindings, depth + 1);
		} else {
			/* types don't match */
			return {false, string_format("%s <> %s with attempted bindings %s",
					a->str().c_str(),
					b->str().c_str(),
					str(bindings).c_str()), {}};
		}
	} else {
		/* types don't match */
		return {false, string_format("%s <> %s with attempted bindings %s",
				a->str().c_str(),
				b->str().c_str(),
				str(bindings).c_str()), {}};
	}
}
