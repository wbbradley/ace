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
	debug_above(9, log("attempting to prune %s from %s",
				::str(bindings).c_str(),
				t->str().c_str()));

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
		env_t::ref env,
        const types::type_t::map &bindings)
{
	static auto type_constraints = type_id(make_iid("true"));

	unification_t unification = unify_core(lhs, rhs, env, bindings, 0, 0, true);

	if (unification.result) {
		for (auto type_constraint: unification.type_constraints) {
			/* map the unification bindings onto the type constraints */
			type_constraint = type_constraint->rebind(unification.bindings);

#ifdef ZION_DEBUG
			if (type_constraint->ftv_count() != 0) {
				debug_above(9, log("type_constraint=%s has free variables", type_constraint->str().c_str()));
			}
#endif

			types::type_t::ref value = type_constraint->eval(env);

			if (!types::is_type_id(value, TRUE_TYPE, nullptr)) {
				unification.result = false;
				unification.reasons = string_format(
						"type constraints %s evaluated to %s",
						type_constraint->str().c_str(),
						value->str().c_str());
				break;
			}
		}
	}

	return unification;
}

bool unifies(types::type_t::ref a, types::type_t::ref b, env_t::ref env) {
    return unify(a, b, env).result;
}

unification_t unify_core(
		const types::type_t::ref &lhs_,
		const types::type_t::ref &rhs_,
		env_t::ref env,
		types::type_t::map bindings,
		int coercions,
		int depth,
		bool allow_variance)
{
	if (depth > 20) {
		log(log_error, "unification depth is getting big...");
		dbg();
	}

	assert(lhs_ != nullptr);
	assert(rhs_ != nullptr);

	INDENT(7,
			string_format("unify_core(%s, %s, ..., %s)",
				lhs_->str().c_str(),
				rhs_->str().c_str(),
				str(bindings).c_str()));

	const auto lhs = lhs_->rebind(bindings)->eval(env);
	const auto rhs = rhs_->rebind(bindings)->eval(env);

	if (lhs == type_bottom() || rhs == type_bottom()) {
		return {true, "bottom type is subtype to everything", bindings, coercions + 1, {}};
	}

	auto ptref_lhs = dyncast<const types::type_ref_t>(lhs);

	if (ptref_lhs != nullptr) {
		auto ptref_rhs = dyncast<const types::type_ref_t>(rhs);
		if (ptref_rhs != nullptr) {
			return unify_core(ptref_lhs->element_type, ptref_rhs->element_type, env, bindings, 0, 0, allow_variance);
		} else {
			return {false, "lhs was expecting a reference type", bindings, coercions, {}};
		}
	} else if (auto ptref_rhs = dyncast<const types::type_ref_t>(rhs)) {
		// if depth isn't 0, then all bets are off, but for now I think it's impossible to not be
		assert(depth == 0);

		/* we can safely ignore if the rhs is a reference because the coercions will auto-deref it */
		return unify_core(lhs, ptref_rhs->element_type, env, bindings, coercions + 1, 0, allow_variance);
	}

	auto pruned_a = prune(lhs, bindings);
	auto pruned_b = prune(rhs, bindings);

	if (pruned_a->repr() == pruned_b->repr()) {
		return {true, "", bindings, coercions, {}};
	}

	auto a = pruned_a->eval(env);
	auto b = pruned_b->eval(env);
	// log("a = %s", a->str().c_str());
	// log("b = %s", b->str().c_str());

	auto ptm_a = dyncast<const types::type_maybe_t>(a);
	auto ptm_b = dyncast<const types::type_maybe_t>(b);

	auto pti_a = dyncast<const types::type_id_t>(a);
	auto pti_b = dyncast<const types::type_id_t>(b);

	auto pto_a = dyncast<const types::type_operator_t>(a);
	auto pto_b = dyncast<const types::type_operator_t>(b);

	auto ptr_a = dyncast<const types::type_ptr_t>(a);
	auto ptr_b = dyncast<const types::type_ptr_t>(b);

	auto ptI_a = dyncast<const types::type_integer_t>(a);
	auto ptI_b = dyncast<const types::type_integer_t>(b);

	auto ptp_a = dyncast<const types::type_product_t>(a);

	auto ptf_a = dyncast<const types::type_function_t>(a);
	auto ptc_a = dyncast<const types::type_function_closure_t>(a);

	auto ptd_a = dyncast<const types::type_data_t>(a);
	auto ptd_b = dyncast<const types::type_data_t>(b);

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
					{BOOL_TYPE, FALSE_TYPE},
					{BOOL_TYPE, TRUE_TYPE},
				};

				static constexpr auto len_coercions = sizeof(coercions_table)/sizeof(coercions_table[0]);
				for (unsigned i = 0; i < len_coercions; ++i) {
					if (a_name == coercions_table[i].to && b_name == coercions_table[i].from) {
						return {true, "", bindings, coercions + 1, {}};
					}
				}
				return {false, "type ids do not match", bindings, coercions, {}};
			}
		}
	}

	if (ptd_a != nullptr) {
		if (ptd_b != nullptr) {
			if (ptd_a->name.text != ptd_b->name.text || ptd_a->type_vars.size() != ptd_b->type_vars.size()) {
#if 0
				if (ptd_a->name.text == "Maybe" && depth == 0) {
					assert(ptd_a->type_vars.size() == 1);
					return unify_core(
							ptd_a->type_vars[0], b, 
							env, bindings, coercions, depth, allow_variance);
				}
#endif
				return {false, "type mismatch", bindings, coercions, {}};
			} else {
				for (int i = 0; i < ptd_a->type_vars.size(); ++i) {
					unification_t unification = unify_core(
							ptd_a->type_vars[i], ptd_b->type_vars[i], env, bindings, coercions, depth, false /*allow_variance*/);
					if (unification.result) {
						bindings = unification.bindings;
						coercions += unification.coercions;
					} else {
						return {false, "type mismatch", bindings, coercions, {}};
					}
				}
				return {true, "", bindings, coercions, {}};
			}
		} else {
#if 0
			if (ptd_a->name.text == "Maybe" && depth == 0) {
				if (types::is_type_id(b, NULL_TYPE, nullptr)) {
					return {true, "", bindings, coercions + 1, {}};
				} else {
					assert(ptd_a->type_vars.size() == 1);
					return unify_core(
							ptd_a->type_vars[0], b, 
							env, bindings, coercions, depth, allow_variance);
				}
			}
#endif
		}
	}

	if (ptI_a != nullptr) {
		if (ptI_b == nullptr) {
			// sanity check: a second full-eval should not do anything...
#if ZION_DEBUG
			ptI_b = dyncast<const types::type_integer_t>(b->eval(env));
#endif
			assert(ptI_b == nullptr);
		}

		if (depth == 0) {
			if (ptI_b != nullptr) {
				return {true, "", bindings, coercions + 1, {}};
			} else if (types::is_type_id(b, CHAR_TYPE, nullptr)) {
				/* we can cast this char to whatever */
				return {true, "", bindings, coercions + 1, {}};
			}
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
			return unify_core(ptm_a->just, ptm_b->just, env, bindings, coercions, depth + 1, allow_variance);
		} else if (types::is_type_id(b, NULL_TYPE, nullptr)) {
			debug_above(7, log("matching null"));
			return {true, "", bindings, coercions + 1, {}};
		} else {
			debug_above(7, log("matching maybe on the lhs"));
			return unify_core(ptm_a->just, b, env, bindings, coercions + 1, depth, allow_variance);
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
					auto unification = unify_core(*a_dims_iter, *b_dims_iter, env, bindings, 0, depth, allow_variance);
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
			auto args_unification = unify_core(ptf_a->args, ptf_b->args, env, bindings, 0, depth, allow_variance);
			if (!args_unification.result) {
				return {false, args_unification.reasons, {}, coercions + args_unification.coercions, {}};
			}
			bindings = args_unification.bindings;
			coercions += args_unification.coercions;
			types::type_t::refs type_constraints;
			type_constraints.swap(args_unification.type_constraints);

			debug_above(7, log("matching function return types"));
			/* finally, make sure the return types unify_core */
			auto return_type_unification = unify_core(ptf_a->return_type, ptf_b->return_type, env, bindings, 0, depth, allow_variance);
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
	} else if (ptc_a != nullptr) {
		if (auto ptf_b = dyncast<const types::type_function_t>(b)) {
			/* allow coercions for unbound function to bound functions */
			return unify_core(ptc_a->function, ptf_b, env, bindings, coercions + 1, depth + 1, allow_variance);
		} else if (auto ptc_b = dyncast<const types::type_function_closure_t>(b)) {
			return unify_core(ptc_a->function, ptc_b->function, env, bindings, coercions, depth, allow_variance);
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
	} else if (pto_a != nullptr) {
		debug_above(7, log(log_info, "checking inbound type_operator %s",
					pto_a->str().c_str()));
		if (pto_b != nullptr) {
			debug_above(7, log(log_info, "checking outbound type_operator %s",
						pto_b->str().c_str()));
			auto unification = unify_core(pto_a->oper, pto_b->oper, env, bindings, 0, depth + 1, allow_variance);
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
				return unify_core(
						pto_a->operand,
					   	pto_b->operand,
					   	env,
					   	bindings,
					   	coercions,
					   	depth + 1,
					   	false /*allow_variance*/);
			}
		}
		return {false, string_format("%s <> %s", a->str().c_str(), b->str().c_str()), bindings, coercions, {}};
	} else if (ptr_a != nullptr) {
		auto a_element_type = ptr_a->element_type->eval(env);

		if (depth == 0 && types::is_type_id(a_element_type, CHAR_TYPE, nullptr)) {
			if (types::is_type_id(b, MANAGED_STR, nullptr)) {
				return {true, "", bindings, coercions + 1, {}};
			}
		}

		if (ptr_b != nullptr) {
			/* handle pointer to void here, rather than making void the top type */
			if (types::is_type_id(a_element_type, VOID_TYPE, nullptr)) {
				/* managed pointers cannot be passed to *void because that seems dangerous.
				 * if you really want to do that, cast it to *void yourself first. */
				assert(dyncast<const types::type_managed_t>(ptr_b->element_type) == nullptr);
				return {true, "", bindings, coercions, {}};
			}

			debug_above(7, log("matching ptr types"));
			return unify_core(ptr_a->element_type, ptr_b->element_type, env, bindings, coercions, depth + 1, allow_variance);
		} else if (types::is_type_id(b, NULL_TYPE, nullptr)) {
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
