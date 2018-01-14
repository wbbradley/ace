#pragma once
#include "ptr.h"
#include "assert.h"
#include <memory>
#include <vector>
#include <map>
#include "types.h"

struct unification_t {
	unification_t() = delete;
	unification_t(
			bool result,
			std::string reasons,
			types::type_t::map bindings,
			int coercions);

	std::string str() const { return reasons + " " + ::str(bindings); }

	/* result */
	bool result;

	/* reasons for the result */
	std::string reasons;

	/* the bindings for any quantified types which we end up with after the
	 * unification process */
	types::type_t::map bindings;

	/* the count of coercions necessary in order to perform this unification */
	int coercions;
};

unification_t unify(
		types::type_t::ref a,
		types::type_t::ref b,
		const types::type_t::map &env,
        types::type_t::map bindings = {},
		int coercions=0,
        int depth=0);

bool unifies(
		types::type_t::ref a,
		types::type_t::ref b,
		const types::type_t::map &env);
