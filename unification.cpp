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
		types::type_t::map bindings) :
	result(result),
	reasons(reasons),
	bindings(bindings)
{
	debug_above(10, log(log_info, "unification result {%s, %s, %s}",
				result ? "success" : "failure", reasons.c_str(),
				::str(bindings).c_str()));
}

types::type_t::ref prune(types::type_t::ref t, const types::type_t::map &bindings) {
	/* Follow the links across the bindings to reach the final binding. */
	std::string type_variable_name;
	if (get_type_variable_name(t, type_variable_name)) {
		auto binding_iter = bindings.find(type_variable_name);
        if (binding_iter != bindings.end()) {
            return prune(binding_iter->second, bindings);
		}
	}

	return t;
}

template <typename T>
bool occurs_in(const ptr<types::type_variable_t> &var, const T &types) {
    /* checks whether a type variable occurs in any other types. */
	for (auto &type : types) {
		if (occurs_in_type(var, type)) {
			return true;
		}
	}
	return false;
}

bool occurs_in_type(
		types::type_variable_t::ref var,
	   	types::type_t::ref b,
	   	types::type_t::map bindings)
{
	/* checks whether a type variable occurs in a type expression. must be
	 * called with var already pruned */
	assert(var == prune(var, bindings));
    auto pruned_b = prune(b, bindings);

	if (auto var2 = dyncast<const types::type_variable_t>(pruned_b)) {
		return var2 == var;
	} else if (auto type_operator = dyncast<const types::type_operator_t>(pruned_b)) {
		return occurs_in_type(var, type_operator->oper, bindings) ||
			occurs_in_type(var, type_operator->operand, bindings);
	} else {
		// TODO: handle type_product, type_sum
		return false;
	}
}

unification_t unify(
		types::type_t::ref lhs,
		types::type_t::ref rhs,
		types::type_t::map env,
		types::type_t::map bindings,
		int depth)
{
	if (depth > 120) {
		log(log_error, "unification depth is getting big...");
		dbg();
	}

	assert(lhs != nullptr);
	assert(rhs != nullptr);

	INDENT(7,
			string_format("unify(%s, %s, ..., %s)",
				lhs->str().c_str(),
				rhs->str().c_str(),
				str(bindings).c_str()));

	auto a = prune(lhs, bindings);
	auto b = prune(rhs, bindings);

	auto ptm_a = dyncast<const types::type_maybe_t>(a);
	auto ptm_b = dyncast<const types::type_maybe_t>(b);

	auto pti_a = dyncast<const types::type_id_t>(a);
	auto pti_b = dyncast<const types::type_id_t>(b);

	auto pto_a = dyncast<const types::type_operator_t>(a);
	auto pto_b = dyncast<const types::type_operator_t>(b);

	auto pts_a = dyncast<const types::type_sum_t>(a);
	auto pts_b = dyncast<const types::type_sum_t>(b);

	auto ptr_a = dyncast<const types::type_ptr_t>(a);
	auto ptr_b = dyncast<const types::type_ptr_t>(b);

	auto ptI_a = dyncast<const types::type_integer_t>(a);
	auto ptI_b = dyncast<const types::type_integer_t>(b);

	auto ptp_a = dyncast<const types::type_product_t>(a);

	auto ptf_a = dyncast<const types::type_function_t>(a);

	if (pti_a != nullptr) {
		/* check for basic type_id matching */
		if (pti_b != nullptr &&
				pti_a->id->get_name() == pti_b->id->get_name())
		{
			/* simple type_id match */
			return {true, "", bindings};
		} else if (pti_a->id->get_name() == "void") {
			/* everything is covariant with void because void encompasses anything (since it cannot be accessed and does
			 * not have a known size) */
			if (auto ptv_b = dyncast<const types::type_variable_t>(b)) {
				// TODO: probably we need to map all ftvs in b to void, not just the outer b
				bindings[ptv_b->id->get_name()] = a;
			}
			return {true, "", bindings};
		}
	}

	if (ptI_a != nullptr) {
	   	if (ptI_b == nullptr) {
			ptI_b = dyncast<const types::type_integer_t>(full_eval(b, env));
		}

		if (ptI_b != nullptr) {
			auto bit_size_unification = unify(ptI_a->bit_size, ptI_b->bit_size, env, bindings, depth + 1);
			if (bit_size_unification.result) {
				return {true, "", bit_size_unification.bindings};
			} else {
				return {
					false,
						string_format("bit-sizes did not match on %s and %s", a->str().c_str(), b->str().c_str()),
						bindings};
			}
		}
	}

	if (auto ptl_a = dyncast<const types::type_literal_t>(a)) {
		if (auto ptl_b = dyncast<const types::type_literal_t>(b)) {
			return {
				(ptl_a->token.text == ptl_b->token.text
				 && ptl_a->token.tk == ptl_b->token.tk),
				"", bindings};
		}
	}

	if (auto ptv_a = dyncast<const types::type_variable_t>(a)) {
		if (a != b) {
			if (occurs_in_type(ptv_a, b, bindings)) {
				return {
					false,
					string_format("recursive unification on %s and %s",
							a->str().c_str(), b->str().c_str()),
					bindings};
			}
			debug_above(4, log(log_info,
						"binding type_variable " c_id("%s") " to " c_type("%s"),
						ptv_a->id->get_name().c_str(),
						b->str(bindings).c_str()));
			assert(bindings.find(ptv_a->id->get_name()) == bindings.end());
			if (b->rebind(bindings)->ftv_count() != 0) {
				debug_above(4, log(log_info,
							"note that %s is itself not fully bound", b->str().c_str()));
			}
			bindings[ptv_a->id->get_name()] = b;
		}

		return {true, "", bindings};
	} else if (auto ptv_b = dyncast<const types::type_variable_t>(b)) {
		debug_above(7, log("flipping type variable from rhs to lhs"));
		return unify(ptv_b, a, env, bindings, depth + 1);
	} else if (ptm_a != nullptr) {
		if (ptm_b != nullptr) {
			debug_above(7, log("matching maybe types"));
			return unify(ptm_a->just, ptm_b->just, env, bindings, depth + 1);
		} else if (b->is_nil()) {
			debug_above(7, log("matching nil"));
			return {true, "", bindings};
		} else {
			debug_above(7, log("matching maybe on the lhs"));
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
			return {
				false,
				string_format("(%s != %s) and (%s could not be expanded further)",
						a->str().c_str(),
						b->str().c_str(),
						a->str().c_str()),
				{}};
		}
	} else if (ptp_a != nullptr) {
		if (auto ptp_b = dyncast<const types::type_product_t>(b)) {
			if (ptp_a->get_pk() != ptp_b->get_pk()) {
				return {
					false,
					string_format("product kinds are different %s <> %s (%s != %s)",
							pkstr(ptp_a->get_pk()),
							pkstr(ptp_b->get_pk()),
							ptp_a->str().c_str(),
							ptp_b->str().c_str()),
					bindings};
			}
			auto a_dimensions = ptp_a->get_dimensions();
			auto b_dimensions = ptp_b->get_dimensions();
			if (a_dimensions.size() != b_dimensions.size()) {
				return {
					false,
					string_format("product type lengths do not match "
							"(a = %s, b = %s)", ptp_a->str().c_str(),
							ptp_b->str().c_str()),
					bindings};
			} else {
				auto a_dims_end = a_dimensions.end();
				auto b_dims_iter = b_dimensions.begin();
				for (auto a_dims_iter = a_dimensions.begin();
						a_dims_iter != a_dims_end;
						++a_dims_iter, ++b_dims_iter) {
					debug_above(7, log("matching subitem in product type"));
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
			return {
				false,
				string_format("%s <> %s",
						a->str().c_str(),
						b->str().c_str()),
				bindings};
		}
	} else if (ptf_a != nullptr) {
		if (auto ptf_b = dyncast<const types::type_function_t>(b)) {
			debug_above(7, log("matching function arguments"));
			/* now make sure the arguments unify */
			auto args_unification = unify(ptf_a->args, ptf_b->args,
					env, bindings, depth + 1);
			if (!args_unification.result) {
				return {false, args_unification.reasons, {}};
			}
			bindings = args_unification.bindings;

			debug_above(7, log("matching function return types"));
			/* finally, make sure the return types unify */
			auto return_type_unification = unify(ptf_a->return_type, ptf_b->return_type,
					env, bindings, depth + 1);
			if (!return_type_unification.result) {
				return {false, return_type_unification.reasons, {}};
			}
			bindings = return_type_unification.bindings;
			return {true, "functions match", bindings};
		} else {
			return {
				false,
				string_format("%s <> %s",
						a->str().c_str(),
						b->str().c_str()),
				bindings};
		}
	} else if (pts_a != nullptr) {
		if (pts_b == nullptr) {
			std::vector<std::string> reasons;
			for (auto option : pts_a->options) {
				debug_above(7, log("matching option of sum type against rhs"));
				auto unification = unify(option, b, env, bindings, depth + 1);
				if (unification.result) {
					if (unification.bindings.size() > bindings.size()) {
						debug_above(2, log(log_info, "replacing bindings %s with %s",
									str(bindings).c_str(),
									str(unification.bindings).c_str()));
					}
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
				debug_above(7, log("checking inbound %s against lhs %s",
							inbound_option->repr().c_str(), a->repr().c_str()));
				auto unification = unify(a, inbound_option, env, bindings, depth + 1);
				if (unification.result) {
					bindings = unification.bindings;
				} else {
					return {
						false,
						string_format(
								"\n\tcould not find a match for \n\t\t%s"
								"\n\tin\n\t\t%s",
								inbound_option->str(bindings).c_str(),
								a->str(bindings).c_str()),
						bindings};
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

				debug_above(7, log("matching type operands"));
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
			return {
				false,
				string_format("%s <> %s with attempted bindings %s",
						a->str().c_str(),
						b->str().c_str(),
						str(bindings).c_str()),
				{}};
		}
	} else if (ptr_a != nullptr) {
		if (ptr_b != nullptr) {
			debug_above(7, log("matching ptr types"));
			return unify(ptr_a->element_type, ptr_b->element_type, env, bindings, depth + 1);
		} else if (b->is_nil()) {
			if (dyncast<const types::type_managed_t>(ptr_a->element_type) == nullptr) {
				/* managed types cannot take nil, because they are guarded by the maybe type */
				return {true, "", bindings};
			} else {
				return {
					false,
						string_format("%s <> %s with attempted bindings %s because managed types cannot receive nil, unless they are guarded by a maybe (in other words, use a ? after the left-hand-side type name)",
								a->str().c_str(),
								b->str().c_str(),
								str(bindings).c_str()),
						{}};
			}
		} else {
			return {
				false,
					string_format("%s <> %s with attempted bindings %s",
							a->str().c_str(),
							b->str().c_str(),
							str(bindings).c_str()),
					{}};
		}
	} else {
		/* types don't match */
		return {
			false,
			string_format("%s <> %s with attempted bindings %s",
					a->str().c_str(),
					b->str().c_str(),
					str(bindings).c_str()),
			{}};
	}
}

bool unifies(
		types::type_t::ref a,
		types::type_t::ref b,
		const types::type_t::map &env)
{
    unification_t unification = unify(a, b, env);
    return unification.result;
}
