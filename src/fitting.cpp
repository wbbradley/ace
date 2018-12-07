#include "zion.h"
#include "scopes.h"
#include "bound_var.h"
#include "callable.h"
#include "fitting.h"
#include "unification.h"
#include "binding.h"


size_t fittings_t::size() const {
	return fittings.size();
}

void fittings_t::reserve(size_t i) {
	fittings.reserve(i);
}

void fittings_t::clear() {
	fittings.resize(0);
}

void fittings_t::push_back(const fitting_t &fitting) {
	assert(!contains(fitting.fn));
	fittings.push_back(fitting);
}

bool fittings_t::contains(var_t::ref fn) const {
    for (auto fitting : fittings) {
        if (fitting.fn->get_type()->get_signature() == fn->get_type()->get_signature()) {
            return true;
        }
    }
    return false;
}

var_t::ref get_best_fit(
		delegate_t &delegate,
		scope_t::ref scope,
		location_t location,
		std::string alias,
		types::type_t::ref args,
		types::type_t::ref return_type,
		var_t::refs &fns,
		fittings_t &fittings,
		bool allow_coercions)
{
	fittings.clear();
	fittings.reserve(fns.size());

	bindings_set_t checked_bindings;

	for (auto &fn : fns) {
		int coercions = 0;

		var_t::ref callable = check_bound_func_vs_callsite(
				delegate, scope, location, fn, args, return_type, coercions, checked_bindings);

		if (callable != nullptr && (coercions == 0 || allow_coercions) && !fittings.contains(callable)) {
			fittings.push_back({fn, callable, coercions});
		} else {
			debug_above(8, log("not adding callable %s to fittings",
						callable ? callable->str().c_str() : "<null>"));
		}
	}

	return fittings.get_best_fitting(location, alias, args, return_type);
}

var_t::ref fittings_t::get_best_fitting(
		location_t location,
		std::string alias, 
		types::type_t::ref args,
		types::type_t::ref return_type)
{
	if (fittings.size() == 1) {
		return fittings[0].fn;
	} else if (fittings.size() == 0) {
		return nullptr;
	} else {
		/* we have multiple matches. however, if one and only one has no coercions, then we'll
		 * accept that as the winner */
		std::sort(fittings.begin(), fittings.end(), [] (const fitting_t &lhs, const fitting_t &rhs) -> bool {
			/* use the most generic fn that matched, because it will generally be the most efficient (fewer cases per
			 * switch) */
			return lhs.fn->get_type()->ftv_count() > rhs.fn->get_type()->ftv_count();
		});

		var_t::ref winner;
		try {
			for (auto fitting : fittings) {
				if (fitting.coercions == 0) {
					if (winner == nullptr) {
						winner = fitting.fn;
					} else {
						throw user_error(location,
								"multiple non-coercing overloads found for %s%s %s",
								alias.c_str(),
								args->str().c_str(),
								return_type != nullptr ? return_type->str().c_str() : "");
					}
				}
			}

			if (winner == nullptr) {
				throw user_error(location,
						"multiple coercing overloads found for %s",
						alias.c_str());
			}

			/* ok, we'll use the one that doesn't involve coercions */
			debug_above(5, log("picked %s because it does not have coercions", winner->str().c_str()));
			return winner;
		} catch (user_error &e) {
			for (auto fitting : fittings) {
				e.add_info(fitting.fn->get_location(),
						"matching %soverload : %s",
						(dyncast<const bound_var_t>(fitting.var_fn) != nullptr) ? c_var("bound ") : c_good("unchecked "),
						fitting.fn->get_type()->str().c_str());
			}
			throw;
		}
	}
}
