#include "zion.h"
#include "scopes.h"
#include "bound_var.h"
#include "callable.h"
#include "fitting.h"


bool function_exists_in(bound_var_t::ref fn, const fittings_t &fittings) {
    for (auto fitting : fittings) {
        if (fitting.fn->get_signature() == fn->get_signature()) {
            return true;
        }
    }
    return false;
}

bound_var_t::ref get_best_fit(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		std::string alias,
		types::type_t::ref args,
		types::type_t::ref return_type,
		var_t::refs &fns,
		bool allow_coercions)
{
	fittings_t fittings;
	fittings.reserve(fns.size());

	for (auto &fn : fns) {
		int coercions = 0;
		bound_var_t::ref callable = check_func_vs_callsite(builder,
				scope, location, fn, args, return_type, coercions);

		if (callable != nullptr && (coercions == 0 || allow_coercions)) {
            if (!function_exists_in(callable, fittings)) {
                fittings.push_back({fn, callable, coercions});
            }
		}
	}

	if (fittings.size() == 1) {
		return fittings[0].fn;
	} else if (fittings.size() == 0) {
		return nullptr;
	} else {
		/* we have multiple matches. however, if one and only one has no coercions, then we'll
		 * accept that as the winner */
		bound_var_t::ref winner;
		std::sort(fittings.begin(), fittings.end(), [] (const fitting_t &lhs, const fitting_t &rhs) -> bool {
			/* use the most generic fn that matched, because it will generally be the most efficient (fewer cases per
			 * switch) */
			return lhs.fn->type->get_type()->ftv_count() > rhs.fn->type->get_type()->ftv_count();
		});

		try {
			for (auto fitting : fittings) {
				if (fitting.coercions == 0) {
					if (winner == nullptr) {
						winner = fitting.fn;
					} else {
						if (winner->get_location() != fitting.fn->get_location()) {
							throw user_error(location,
									"multiple (noncoercing) overloads found for %s%s %s",
									alias.c_str(),
									args->str().c_str(),
									return_type != nullptr ? return_type->str().c_str() : "");
						}
					}
				}
			}

			if (winner == nullptr) {
				throw user_error(location,
						"multiple (coercion) overloads found for %s",
						alias.c_str());
			}

			/* ok, we'll use the one that doesn't involve coercions */
			debug_above(5, log("picked %s because it does not have coercions",
						winner->str().c_str()));
			return winner;
		} catch (user_error &e) {
			for (auto fitting : fittings) {
				e.add_info(fitting.fn->get_location(),
						"matching %soverload : %s",
						(dyncast<const bound_var_t>(fitting.var_fn) != nullptr) ? c_var("bound ") : c_good("unchecked "),
						fitting.fn->type->get_type()->str().c_str());
			}
			throw;
		}
	}
}
