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
			types::type::map bindings) :
		result(result),
		reasons(reasons),
		bindings(bindings) {}

	std::string str() const { return reasons + " " + ::str(bindings); }

	/* result */
	bool result;

	/* reasons for the result */
	std::string reasons;

	/* the bindings for any quantified types which we end up with after the
	 * unification process */
	types::type::map bindings;
};

unification_t unify(
		status_t &status,
		types::term::ref a,
		types::term::ref b,
		types::term::map env);
