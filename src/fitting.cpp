#include "zion.h"
#include "scopes.h"
#include "bound_var.h"
#include "callable.h"
#include "fitting.h"


bool function_exists_in(var_t::ref fn, const fittings_t &fittings) {
	location_t location = fn->get_location();

    for (auto callable : fittings) {
		if (callable.fn->get_location() == location) {
			debug_above(7,
					log("function %s at %s exists as %s at %s in fittings",
						fn->str().c_str(),
						fn->get_location().str().c_str(),
						callable.fn->str().c_str(),
						callable.fn->get_location().str().c_str()));
			return true;
		}
    }
    return false;
}

bound_var_t::ref get_best_fit(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		std::string alias,
		types::type_t::ref args,
		types::type_t::ref return_type,
		var_t::refs &fns)
{
	fittings_t fittings;
	fittings.reserve(fns.size());

	for (auto &fn : fns) {

		if (function_exists_in(fn, fittings)) {
			/* we've already found a version of this function,
			 * let's not bind it again */

			// REVIEW: i think this is broken because if we bound if first with a managed type and
			// then we want to rebind it with a native type, this will skip that rebinding, and
			// choose the managed type version. need to make the function instantiation code more
			// robust against re-instantiating the same signature, and then remove this check.

			debug_above(7, log(log_info,
						"skipping checking %s because we've already got a matched version of that function",
						fn->str().c_str()));
			continue;
		}

		int coercions = 0;
		bound_var_t::ref callable = check_func_vs_callsite(status, builder,
				scope, location, fn, args, return_type, coercions);

		if (!status) {
			assert(callable == nullptr);
			return nullptr;
		} else if (callable != nullptr) {
			fittings.push_back({callable, coercions});
		}
	}

	assert(!!status);
	if (fittings.size() == 1) {
		return fittings[0].fn;
	} else if (fittings.size() == 0) {
		return nullptr;
	} else {
		/* we have multiple matches. however, if one and only one has no coercions, then we'll
		 * accept that as the winner */
		bound_var_t::ref winner;
		for (auto fitting : fittings) {
			if (fitting.coercions == 0) {
				if (winner == nullptr) {
					winner = fitting.fn;
				} else {
					user_error(status, location,
							"multiple (noncoercing) overloads found for %s",
							alias.c_str());
				}
			}
		}

		if (!!status) {
			if (winner == nullptr) {
				user_error(status, location,
						"multiple (coercion) overloads found for %s",
						alias.c_str());
			}
		}

		if (!!status) {
			/* ok, we'll use the one that doesn't involve coercions */
			debug_above(5, log("picked %s because it does not have coercions",
						winner->str().c_str()));
			return winner;
		} else {
			for (auto fitting : fittings) {
				user_message(log_info, status, fitting.fn->get_location(),
						"matching overload : %s",
						fitting.fn->type->get_type()->str().c_str());
			}
		}
	}

	assert(!status);
	return nullptr;
}
