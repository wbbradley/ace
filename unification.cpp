#include "zion.h"
#include "dbg.h"
#include <sstream>
#include "utils.h"
#include "types.h"
#include "unification.h"

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


#if 0
std::string unification_t::str() const {
	std::stringstream ss;
	ss << "[";
	const char *sep = "";
	for (auto &term_pair : bindings) {
		ss << sep << C_UNCHECKED << term_pair.first.str() << C_RESET;
		ss << " => " << term_pair.second->str();
		sep = ", ";
	}
	ss << "]";
	return ss.str();
}
#endif

types::type::ref unroll(
		types::type::ref type,
	   	types::term::map env,
	   	types::type::map bindings)
{
    /* Handle macro expansion of one level. type_refs can be expanded. */
    if (auto type_ref = dyncast<const types::type_ref>(type)) {
        auto type_ref_lambdified = type_ref->to_term(bindings);
        auto type_ref_reduced = type_ref_lambdified->evaluate(env, 1);

		debug_above(5, log(log_info, "Unrolled:\n\t%r\n\t%s",
			type->str().c_str(),
		   	type_ref_reduced->get_type()->str(bindings).c_str()));

        return type_ref_reduced->get_type();
	} else {
        return type;
	}
}

unification_t unify_core(
		types::type::ref lhs,
		types::type::ref rhs,
		types::term::map env,
		types::type::map bindings)
{
	debug_above(4, log(log_info, "attempting to unify %s and %s", lhs->str().c_str(), rhs->str().c_str()));

    auto pruned_a = prune(lhs, bindings);
    auto pruned_b = prune(rhs, bindings);


    if (pruned_a->str(bindings) == pruned_b->str(bindings)) {
		log(log_info, "matched " c_type("%s"), pruned_a->str(bindings).c_str());
        return {true, "", bindings};
	}

	if (auto ptr_a = dyncast<const struct ::types::type_ref>(pruned_a)) {
		if (auto ptr_b = dyncast<const struct ::types::type_ref>(pruned_b)) {
			auto a_macro_term = ptr_a->macro->to_term(bindings)->evaluate(env, 0);
			auto b_macro_term = ptr_b->macro->to_term(bindings)->evaluate(env, 0);
			if (a_macro_term->repr() == b_macro_term->repr()) {
				assert(false);
				return {false, "not impl", bindings};
				// unify_core(reduce(TypeOperator, pruned_a.args),
				//		reduce(TypeOperator, pruned_b.args),
				//		env, bindings)
			} else {
				debug_above(3, log(log_info, "unmatched refs: %s %s",
						   	a_macro_term->repr().c_str(),
							b_macro_term->repr().c_str()));
			}
		}
	}


    auto a = unroll(pruned_a, env, bindings);
    auto b = unroll(pruned_b, env, bindings);

    if (a->str(bindings) == b->str(bindings)) {
		log(log_info, "matched " c_type("%s"), pruned_a->str(bindings).c_str());
        return {true, "", bindings};
	}

	if (auto ptv = dyncast<const types::type_variable>(a)) {
		if (a != b) {
			if (occurs_in_type(ptv, b, bindings)) {
				return {
					false,
					string_format("recursive unification on %s and %s",
							a->str().c_str(), b->str().c_str()),
					bindings};
			}
			log(log_info, "binding " c_id("%s") " to " c_type("%s"),
					ptv->id->get_name().c_str(),
					b->str(bindings).c_str());
			assert(bindings.find(ptv->id->get_name()) == bindings.end());
			bindings[ptv->id->get_name()] = b;
		} else {
			assert(false);
		}

		return {true, "", bindings};
	} else {
		if (auto pto_a = dyncast<const types::type_operator>(a)) {
			if (auto pto_b = dyncast<const types::type_operator>(b)) {
				auto unification = unify_core(pto_a->oper, pto_b->oper, env, bindings);
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

				return unify_core(pto_a->operand, pto_b->operand, env, bindings);
			}
		}

		/* types don't match */
        return {false, string_format("%s <> %s",
			   	a->str(bindings).c_str(), b->str(bindings).c_str()), {}};
	}
}

unification_t unify(
		types::term::ref lhs,
		types::term::ref rhs,
		types::term::map env)
{
	log(log_info, "unify(" c_term("%s") ", " c_term("%s") ", %s",
			lhs->str().c_str(), rhs->str().c_str(), str(env).c_str());

	return unify_core(
		   	lhs->evaluate(env, 0)->get_type(),
		   	rhs->evaluate(env, 0)->get_type(),
		   	env,
		   	{});
}

