#include "zion.h"
#include "dbg.h"
#include <sstream>
#include "utils.h"
#include "types.h"

#if 0
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

bool occurs_in_type(const types::type_variable::ref &var, const term_t::ref &term) {
	/* checks whether a type variable occurs in a type expression. must be
	 * called with var already pruned */
	assert(var == var->prune());
    auto pruned_term = term->prune();
	if (auto var2 = dyncast<types::type_variable>(pruned_term)) {
		if (var2 == var) {
			return true;
		}
	} else if (auto term_operator = dyncast<term_operator_t>(pruned_term)) {
        return occurs_in(var, term_operator->terms);
	}
    return false;
}


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

bool unification_t::unify(term_t::ref lhs, term_t::ref rhs) {
	debug_above(7, log(log_info, "attempting to unify %s and %s", lhs->str().c_str(), rhs->str().c_str()));

	auto a = lhs->prune();
	auto b = rhs->prune();

	/* beta reduce any operators we have */
	if (auto pa = dyncast<term_operator_t>(a)) {
		a = pa->beta_reduce(term_map, generics, next_unnamed_id);
	}

	if (auto pb = dyncast<term_operator_t>(b)) {
		b = pb->beta_reduce(term_map, generics, next_unnamed_id);
	}

	if (a == b) {
		/* we shouldn't be seeing generics get through here since we only
		 * try to unify against bound callsites */
		assert(!dyncast<types::type_variable>(a));
		debug_above(6, log(log_info, "unification of %s and %s succeeded", a->str().c_str(),
				b->str().c_str()));
		return true;
	}


	if (auto ptv = dyncast<types::type_variable>(a)) {
		if (occurs_in_type(ptv, b)) {
			log(log_warning, "recursive unification on %s and %s",
					a->str().c_str(), b->str().c_str());
			return false;
		}
		assert(ptv->instance == nullptr);

		/* let's record this variable assignment, which will implicitly
		 * assign all `a` variables to the value of the `b` term */
		ptv->instance = b;
		debug_above(6, log(log_info, "making type variable substitution %s", ptv->str().c_str()));
		return true;
	} else if (dyncast<term_bound_t>(a) && dyncast<types::type_variable>(b)) {
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
}
#endif
