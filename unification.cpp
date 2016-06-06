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
		assert(false);
	}
    return false;
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

unification_t::ref unification_t::attempt(
		const std::vector<std::pair<atom, types::term::ref>> &macros,
		const types::term::ref &a,
		const types::term::ref &b)
{
	/* attempt to create a unification of two terms. the first step is to
	 * translate the terms into more malleable structures */
	assert(!b.is_generic());

	/* create some bookkeeping for all our work */
	unification_t::ref unification = make_ptr<unification_t>(a, b);

	/* let's load any macros from the type context into the term map so that
	 * when those terms occur within other type operators, we'll automatically
	 * inject those macros into the type algebra at that point */
	for (auto &macro_pair : macros) {
		assert(!macro_pair.first.is_generic_type_alias());
		upsert_var_name<term_macro_t>(
				unification->term_map,
				macro_pair.first,
				macro_pair.second);
	}

	/* create the term for the lhs */
	term_t::ref lhs = term_t::adapt(
			unification->term_map,
			unification->generics,
			unification->next_unnamed_id,
			a);

	/* create the term for the rhs */
	term_t::map generics_placeholder;
	term_t::ref rhs = term_t::adapt(
			unification->term_map,
			generics_placeholder,
			unification->next_unnamed_id,
			b);

	assert(generics_placeholder.size() == 0);

	if (unification->unify(lhs, rhs)) {
		return unification;
	} else {
		debug_above(3, log(log_info, "unification of %s and %s failed with %s",
				a.str().c_str(), b.str().c_str(),
				unification->str().c_str()));
		return nullptr;
	}
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
			type->str({}).c_str(),
		   	type_ref_reduced->get_type()->str(bindings).c_str()));

        return type_ref_reduced->get_type();
	} else {
        return type;
	}
}

unification_t::ref unify_core(
		status_t &status,
		types::term::map env,
		types::type::ref lhs,
		types::type::ref rhs,
		types::type::map bindings)
{
	debug_above(7, log(log_info, "attempting to unify %s and %s", lhs->str().c_str(), rhs->str().c_str()));

    auto pruned_a = prune(lhs, bindings);
    auto pruned_b = prune(rhs, bindings);


    if (pruned_a->str(bindings) == pruned_b->str(bindings)) {
        return make_ptr<unification_t>(true, "", bindings);
	}

	if (auto ptr_a = dyncast<const struct ::types::type_ref>(pruned_a)) {
		if (auto ptr_b = dyncast<const struct ::types::type_ref>(pruned_b)) {
			auto a_macro_term = ptr_a->macro->to_term(bindings)->evaluate(env, 0);
			auto b_macro_term = ptr_b->macro->to_term(bindings)->evaluate(env, 0);
			if (a_macro_term->repr() == b_macro_term->repr()) {
				return null_impl();
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
        return make_ptr<unification_t>(true, "", bindings);
	}

	return null_impl();

	if (auto ptv = dyncast<const types::type_variable>(a)) {
		if (a != b) {
			if (occurs_in_type(ptv, b, bindings)) {
				return make_ptr<unification_t>(false, string_format(
							"recursive unification on %s and %s",
							a->str().c_str(), b->str().c_str()),
						bindings);
			}
			assert(bindings.find(ptv->id->get_name()) == bindings.end());
			bindings[ptv->id->get_name()] = b;
		}

		return make_ptr<unification_t>(true, "", bindings);
	}
	return null_impl();

#if 0
   	else if (dyncast<term_bound_t>(a) && dyncast<types::type_variable>(b)) {
		log(log_info, "cannot handle generic input values (%s)", b->str().c_str());
        return false;
	} else if (dyncast<term_operator_t>(a) && dyncast<types::type_variable>(b)) {
		log(log_info, "cannot handle generic input values (%s)", b->str().c_str());
        return false;
	} else if (dyncast<term_bound_t>(a) && dyncast<term_bound_t>(b)) {
		/* these two bound types are just not the same */
		return false;
	} else if (dyncast<term_bound_t>(a) && dyncast<term_operator_t>(b)) {
		/* these two types are just not the same */
		return false;
	} else if (dyncast<term_operator_t>(a) && dyncast<term_bound_t>(b)) {
		/* these two types are just not the same */
		return false;
	} else if (auto ptor = dyncast<term_or_t>(a)) {
		// TODO: consider copying the unification terms map to avoid improper
		// mappings
		for (auto &choice : ptor->choices) {
			if (unify(choice, b)) {
				return true;
			}
		}
		return false;
	} else {
		auto pa = dyncast<term_operator_t>(a);
		auto pb = dyncast<term_operator_t>(b);
		if (pa && pb) {
			if (pa->terms.size() != pb->terms.size()) {
				debug_above(3, log(log_warning, "not unifying %s and %s because |%s| != |%s|",
							lhs_initial.str().c_str(),
							rhs_initial.str().c_str(),
							pa->str().c_str(), pb->str().c_str()));
				return false;
			}

			auto a_iter = pa->terms.begin();
			auto b_iter = pb->terms.begin();
			while (a_iter != pa->terms.end()) {
				auto a_item = *a_iter++;
				auto b_item = *b_iter++;
				if (!unify(a_item, b_item)) {
					debug_above(3, log(log_warning, "not unifying %s and %s because %s != %s",
								lhs_initial.str().c_str(),
								rhs_initial.str().c_str(),
								a_item->str().c_str(), b_item->str().c_str()));
					return false;
				}
			}
			return true;
		} else {
			log(log_warning, "attempted to unify types %s and %s", a->str().c_str(), b->str().c_str());
		}
	}
	return false;
#endif
}

unification_t::ref unify(
		status_t &status,
		types::term::map env,
		types::term::ref lhs,
		types::term::ref rhs)
{
	return unify_core(
			status,
		   	env,
		   	lhs->evaluate(env, 0)->get_type(),
		   	rhs->evaluate(env, 0)->get_type(),
		   	{});
}

