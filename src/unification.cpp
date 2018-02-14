#include "zion.h"
#include "dbg.h"
#include "logger.h"
#include <sstream>
#include "utils.h"
#include "types.h"
#include "unification.h"
#include "scopes.h"


unification_t::unification_t(
		bool result,
		std::string reasons,
		const types::type_t::map &bindings,
		int coercions,
		const types::type_t::refs &type_constraints) :
	result(result),
	reasons(reasons),
	bindings(bindings),
	coercions(coercions),
	type_constraints(type_constraints)
{
	debug_above(10, log(log_info, "unification result {%s, %s, %s, %d, [%s]}",
				result ? "success" : "failure", reasons.c_str(),
				::str(bindings).c_str(), coercions, join_str(type_constraints, ", ").c_str()));
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
		const types::type_t::map &nominal_env,
		const types::type_t::map &total_env)
{
	static auto type_constraints = type_id(make_iid("true"));

	unification_t unification = unify_core(
			lhs, rhs,
			nominal_env,
			{},
			0,
			0);

	if (unification.result) {
		for (auto type_constraint: unification.type_constraints) {
			/* map the unification bindings onto the type constraints */
			type_constraint = type_constraint->rebind(unification.bindings);

			if (type_constraint->ftv_count() != 0) {
				log("type_constraint=%s has free variables", type_constraint->str().c_str());
				dbg();
			}

			types::type_t::ref value = type_constraint->eval(nominal_env, total_env);

			if (!types::is_type_id(value, "true")) {
				unification.result = false;
				unification.reasons = string_format(
						"type constraints evaluated to %s",
						value->str().c_str());
				break;
			}
		}
	}

	return unification;
}

unification_t unify(
		types::type_t::ref lhs,
		types::type_t::ref rhs,
		const scope_t::ref &scope)
{
	auto nominal_env = scope->get_nominal_env();
	auto total_env = scope->get_total_env();

	return unify(lhs, rhs, nominal_env, total_env);
}

bool unifies(
		types::type_t::ref a,
		types::type_t::ref b,
		const types::type_t::map &nominal_env,
		const types::type_t::map &structural_env)
{
    return unify(a, b, nominal_env, structural_env).result;
}

bool unifies(
		types::type_t::ref a,
		types::type_t::ref b,
		const scope_t::ref &scope)
{
    return unify(a, b, scope).result;
}

unification_t unify_core(
		const types::type_t::ref &lhs,
		const types::type_t::ref &rhs,
		const types::type_t::map &env,
		types::type_t::map bindings,
		int coercions,
		int depth)
{
	if (depth > 20) {
		log(log_error, "unification depth is getting big...");
		dbg();
	}

	assert(lhs != nullptr);
	assert(rhs != nullptr);

	INDENT(7,
			string_format("unify_core(%s, %s, ..., %s)",
				lhs->str().c_str(),
				rhs->str().c_str(),
				str(bindings).c_str()));

	auto ptref_lhs = dyncast<const types::type_ref_t>(lhs);

	if (ptref_lhs != nullptr) {
		auto ptref_rhs = dyncast<const types::type_ref_t>(rhs);
		if (ptref_rhs != nullptr) {
			return unify_core(ptref_lhs->element_type, ptref_rhs->element_type, env, bindings, 0, 0);
		} else {
			return {false, "lhs was expecting a reference type", bindings, coercions, {}};
		}
	} else if (auto ptref_rhs = dyncast<const types::type_ref_t>(rhs)) {
		// if depth isn't 0, then all bets are off, but for now I think it's impossible to not be
		assert(depth == 0);

		/* we can safely ignore if the rhs is a reference because the coercions will auto-deref it */
		return unify_core(lhs, ptref_rhs->element_type, env, bindings, coercions + 1, 0);
	}

	auto pruned_a = prune(lhs, bindings);
	auto pruned_b = prune(rhs, bindings);

	if (pruned_a->repr() == pruned_b->repr()) {
		// May need to scrape any type constraints out of the function...
		assert(!dyncast<const types::type_function_t>(pruned_a->eval(env, {})));

		return {true, "", bindings, coercions, {}};
	}

	auto a = pruned_a->eval(env, {});
	auto b = pruned_b->eval(env, {});
	if (dyncast<const types::type_sum_t>(a) && types::is_type_id(b, "bool")) {
		/* make sure we enable checking bool against type sums */
		static auto bool_type = type_sum({type_id(make_iid("true")), type_id(make_iid("false"))}, INTERNAL_LOC());
		b = bool_type;
	} else if (b->is_zero()) {
		/* for the purposes of unification, we can treat zero as a regular integer */
		static auto zero_type = type_integer(
				type_literal({INTERNAL_LOC(), tk_integer, ZION_BITSIZE_STR}),
				type_id(make_iid("true" /*signed*/)));
		b = zero_type;
	}

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
		/* we have reduced down to a type id for a */
		auto a_name = pti_a->id->get_name();

		if (pti_b != nullptr) {
			/* we have reduced down to a type id for b */
			auto b_name = pti_b->id->get_name();

			if (a_name == b_name) {
				/* simple type_id match */
				return {true, "", bindings, coercions, {}};
			} else if (depth == 0) {
				static struct {
					const char *to;
					const char *from;
				} coercions_table[] = {
					{MANAGED_BOOL, BOOL_TYPE},
					{MANAGED_BOOL, FALSE_TYPE},
					{MANAGED_BOOL, TRUE_TYPE},
					{MANAGED_FALSE, FALSE_TYPE},
					{MANAGED_TRUE, TRUE_TYPE},
					{FALSE_TYPE, MANAGED_FALSE},
					{TRUE_TYPE, MANAGED_TRUE},
					{BOOL_TYPE, MANAGED_BOOL},
					{BOOL_TYPE, MANAGED_TRUE},
					{BOOL_TYPE, MANAGED_FALSE},
					{BOOL_TYPE, FALSE_TYPE},
					{BOOL_TYPE, TRUE_TYPE},
					{MANAGED_FLOAT, FLOAT_TYPE},
					{FLOAT_TYPE, MANAGED_FLOAT},
				};

				static constexpr auto len_coercions = sizeof(coercions_table)/sizeof(coercions_table[0]);
				for (unsigned i = 0; i < len_coercions; ++i) {
					if (a_name == coercions_table[i].to && b_name == coercions_table[i].from) {
						return {true, "", bindings, coercions + 1, {}};
					}
				}
				return {false, "type ids do not match", bindings, coercions, {}};
			}
		} else if (depth == 0) {
			if (ptI_b != nullptr && a_name == MANAGED_INT) {
				return {true, "", bindings, coercions + 1, {}};
			} else if (ptI_b != nullptr && a_name == MANAGED_CHAR) {
				return {true, "", bindings, coercions + 1, {}};
			} else if (a_name == MANAGED_STR && (ptr_b != nullptr && types::is_type_id(ptr_b->element_type->eval(env, {}), CHAR_TYPE))) {
				/* we should be able to convert a *char_t to a str */
				return {true, "", bindings, coercions + 1, {}};
			}
		}
	}

	if (ptI_a != nullptr) {
		if (ptI_b == nullptr) {
			// sanity check: a second full-eval should not do anything...
#if ZION_DEBUG
			ptI_b = dyncast<const types::type_integer_t>(b->eval(env, {}));
#endif
			assert(ptI_b == nullptr);
		}

		if (ptI_b != nullptr) {
			return {true, "", bindings, coercions, {}};
		} else if (depth == 0 && types::is_type_id(b, MANAGED_INT)) {
			/* we can cast this int to whatever */
			return {true, "", bindings, coercions + 1, {}};
		} else if (depth == 0 && types::is_type_id(b, MANAGED_CHAR)) {
			/* we can cast this int to whatever */
			return {true, "", bindings, coercions + 1, {}};
		}
	}

	if (auto ptl_a = dyncast<const types::type_literal_t>(a)) {
		if (auto ptl_b = dyncast<const types::type_literal_t>(b)) {
			return {
				(ptl_a->token.text == ptl_b->token.text
				 && ptl_a->token.tk == ptl_b->token.tk),
					"", bindings, coercions, {}};
		}
	}

	if (auto ptv_a = dyncast<const types::type_variable_t>(a)) {
		if (a != b) {
			if (occurs_in_type(ptv_a, b, bindings)) {
				return {
					false,
						string_format("recursive unification on %s and %s",
								a->str().c_str(), b->str().c_str()),
						bindings,
						coercions, {}};
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

		return {true, "", bindings, coercions, {}};
	} else if (auto ptv_b = dyncast<const types::type_variable_t>(b)) {
		if (a != b) {
			if (occurs_in_type(ptv_b, a, bindings)) {
				return {
					false,
					string_format("recursive unification on %s and %s",
							a->str().c_str(), b->str().c_str()),
					bindings,
					coercions, {}};
			}
			debug_above(4, log(log_info,
						"binding type_variable " c_id("%s") " to " c_type("%s"),
						ptv_b->id->get_name().c_str(),
						a->str(bindings).c_str()));
			assert(bindings.find(ptv_b->id->get_name()) == bindings.end());
			if (a->rebind(bindings)->ftv_count() != 0) {
				debug_above(4, log(log_info,
							"note that %s is itself not fully bound", a->str().c_str()));
			}
			bindings[ptv_b->id->get_name()] = a;
		}

		return {true, "", bindings, coercions, {}};
	} else if (ptm_a != nullptr) {
		if (ptm_b != nullptr) {
			debug_above(7, log("matching maybe types"));
			return unify_core(ptm_a->just, ptm_b->just, env, bindings, coercions, depth + 1);
		} else if (b->is_null()) {
			debug_above(7, log("matching null"));
			return {true, "", bindings, coercions, {}};
		} else {
			debug_above(7, log("matching maybe on the lhs"));
			return unify_core(ptm_a->just, b, env, bindings, coercions, depth + 1);
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
					bindings,
					coercions,
					{}};
			}
			auto a_dimensions = ptp_a->get_dimensions();
			auto b_dimensions = ptp_b->get_dimensions();
			if (a_dimensions.size() != b_dimensions.size()) {
				return {
					false,
					string_format("product type lengths do not match "
							"(a = %s, b = %s)", ptp_a->str().c_str(),
							ptp_b->str().c_str()),
					bindings,
					coercions,
					{}};
			} else {
				auto a_dims_end = a_dimensions.end();
				auto b_dims_iter = b_dimensions.begin();
				for (auto a_dims_iter = a_dimensions.begin();
						a_dims_iter != a_dims_end;
						++a_dims_iter, ++b_dims_iter) {
					debug_above(7, log("matching subitem in product type"));
					auto unification = unify_core(*a_dims_iter, *b_dims_iter, env, bindings, 0, depth);
					if (!unification.result) {
						return {false, unification.reasons, {}, coercions, {}};
					}
					bindings = unification.bindings;
					coercions += unification.coercions;
				}

				return {true, "products match", bindings, coercions, {}};
			}
		} else {
			return {
				false,
				string_format("%s <> %s",
						a->str().c_str(),
						b->str().c_str()),
				bindings,
				coercions,
				{}};
		}
	} else if (ptf_a != nullptr) {
		if (auto ptf_b = dyncast<const types::type_function_t>(b)) {
			// NB: it does not yet make sense to unify_core over type constraints...

			debug_above(7, log("matching function arguments"));
			/* now make sure the arguments unify_core */
			auto args_unification = unify_core(ptf_a->args, ptf_b->args, env, bindings, 0, depth);
			if (!args_unification.result) {
				return {false, args_unification.reasons, {}, coercions + args_unification.coercions, {}};
			}
			bindings = args_unification.bindings;
			coercions += args_unification.coercions;
			types::type_t::refs type_constraints;
			type_constraints.swap(args_unification.type_constraints);

			debug_above(7, log("matching function return types"));
			/* finally, make sure the return types unify_core */
			auto return_type_unification = unify_core(ptf_a->return_type, ptf_b->return_type, env, bindings, 0, depth);
			if (!return_type_unification.result) {
				return {false, return_type_unification.reasons, {}, coercions, {}};
			}
			bindings = return_type_unification.bindings;
			coercions += return_type_unification.coercions;
			for (auto type_constraint: return_type_unification.type_constraints) {
				type_constraints.push_back(type_constraint);
			}

			if (ptf_a->type_constraints != nullptr) {
				type_constraints.push_back(ptf_a->type_constraints);
			}
			return {true, "functions match", bindings, coercions, type_constraints};
		} else {
			return {
				false,
				string_format("%s <> %s",
						a->str().c_str(),
						b->str().c_str()),
				bindings,
				coercions,
				{}};
		}
	} else if (pts_a != nullptr) {
		if (pts_b == nullptr) {
			std::vector<std::string> reasons;
			for (auto option : pts_a->options) {
				debug_above(7, log("matching option of sum type against rhs"));
				auto unification = unify_core(option, b, env, bindings, 0, depth);
				if (unification.result) {
					if (unification.bindings.size() > bindings.size()) {
						debug_above(2, log(log_info, "replacing bindings %s with %s",
									str(bindings).c_str(),
									str(unification.bindings).c_str()));
					}
					bindings = unification.bindings;
					coercions += unification.coercions;
					return {true, option->str(bindings), bindings, coercions, {}};
				} else {
					reasons.push_back(unification.reasons);
				}
			}
			return {false, join(reasons, "\n\t"), {}, coercions, {}};
		} else {
			assert(pts_b != nullptr);
			for (auto inbound_option : pts_b->options) {
				debug_above(7, log("checking inbound %s against lhs %s", inbound_option->repr().c_str(), a->repr().c_str()));
				auto unification = unify_core(a, inbound_option, env, bindings, 0, depth);
				if (unification.result) {
					bindings = unification.bindings;
					coercions += unification.coercions;
				} else {
					return {
						false,
						string_format(
								"\n\tcould not find a match for \n\t\t%s"
								"\n\tin\n\t\t%s",
								inbound_option->str(bindings).c_str(),
								a->str(bindings).c_str()),
						bindings,
						coercions,
						{}};
				}
			}
			return {true, "inbound type is a subset of outbound type", bindings, coercions, {}};
		}
	} else if (pts_b != nullptr) {
		for (auto inbound_option : pts_b->options) {
			debug_above(7, log("checking inbound %s against lhs %s", inbound_option->repr().c_str(), a->repr().c_str()));
			auto unification = unify_core(a, inbound_option, env, bindings, 0, depth);
			if (unification.result) {
				bindings = unification.bindings;
				coercions += unification.coercions;
			} else {
				return {
					false,
					string_format(
							"\n\tcould not find a match for \n\t\t%s"
							"\n\tin\n\t\t%s",
							inbound_option->str(bindings).c_str(),
							a->str(bindings).c_str()),
					bindings,
					coercions,
					{}};
			}
		}
		return {true, "inbound type is a subset of outbound type", bindings, coercions, {}};
	} else if (pto_a != nullptr) {
		debug_above(7, log(log_info, "checking inbound type_operator %s",
					pto_a->str().c_str()));
		if (pto_b != nullptr) {
			debug_above(7, log(log_info, "checking outbound type_operator %s",
						pto_b->str().c_str()));
			auto unification = unify_core(pto_a->oper, pto_b->oper, env, bindings, 0, depth + 1);
			if (unification.result) {
				bindings = unification.bindings;
				coercions += unification.coercions;

				if ((pto_a->operand == nullptr) != (pto_b->operand == nullptr)) {
					return {
						false,
						string_format("type mismatch: %s != %s", a->str(bindings).c_str(), b->str(bindings).c_str()),
						{},
						coercions,
						{}};
				}

				assert(pto_a->operand != nullptr && pto_b->operand != nullptr);

				debug_above(7, log("matching type operands"));
				return unify_core(pto_a->operand, pto_b->operand, env, bindings, coercions, depth + 1);
			}
		} else {
			/* fallthrough, and try expanding the left-hand side */
			debug_above(7, log(log_info, "falling through"));
		}
		auto new_a = pto_a->rebind(bindings)->eval(env, {});

		/* apply the bindings first, so as to simplify the application */
		if (new_a != nullptr && types::is_managed_ptr(new_a, {}, {})) {
			/* managed pointers are opaque and should have unified nominally */
			new_a = nullptr;
		}

		if (new_a != pto_a) {
			debug_above(7, log(log_info, "operator %s -> %s",
						pto_a->str().c_str(),
						new_a->str().c_str()));

			unification_t unification = unify_core(new_a, b, env, bindings, coercions, depth + 1);
			if (!unification.result) {
				return {false, string_format("%s <> %s", a->str().c_str(), b->str().c_str()), bindings, coercions, {}};
			} else {
				return unification;
			}
		} else {
			/* types don't match */
			return {
				false,
				string_format("%s <> %s", a->str(bindings).c_str(), b->str(bindings).c_str()),
				{},
				coercions,
				{}};
		}
	} else if (ptr_a != nullptr) {
		auto a_element_type = ptr_a->element_type->eval(env, {});
		if (ptr_b != nullptr) {
			/* handle pointer to void here, rather than making void the top type */
			if (types::is_type_id(a_element_type, "void")) {
				/* managed pointers cannot be passed to *void because that seems dangerous.
				 * if you really want to do that, cast it to *void yourself first. */
				assert(dyncast<const types::type_managed_t>(ptr_b->element_type) == nullptr);
				return {true, "", bindings, coercions, {}};
			}

			debug_above(7, log("matching ptr types"));
			return unify_core(ptr_a->element_type, ptr_b->element_type, env, bindings, coercions, depth + 1);
		} else if (b->is_null()) {
			return {
				false,
				string_format("pointer types cannot accept null unless they are guarded by a maybe (in other words, use a ? after the left-hand-side type name)",
						a->str().c_str(),
						b->str().c_str(),
						str(bindings).c_str()),
				{},
				coercions,
				{}};
		} else {
			return {
				false,
				string_format("pointer types only accept like pointer types",
						a->str().c_str(),
						b->str().c_str(),
						str(bindings).c_str()),
				{},
				coercions,
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
			{},
			coercions,
			{}};
	}
}
