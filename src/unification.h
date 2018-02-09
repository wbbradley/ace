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
			const types::type_t::map &bindings,
			int coercions,
            const types::type_t::refs &type_constraints);

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

    /* the type constraints that need to be checked if this unified */
    types::type_t::refs type_constraints;
};

struct scope_t;

unification_t unify(
		types::type_t::ref a,
		types::type_t::ref b,
        const ptr<scope_t> &scope);

unification_t unify(
		types::type_t::ref a,
		types::type_t::ref b,
		const types::type_t::map &env,
		const types::type_t::map &structural_env);

unification_t unify_core(
		const types::type_t::ref &a,
		const types::type_t::ref &b,
		const types::type_t::map &nominal_env,
        types::type_t::map bindings,
		int coercions,
        int depth);

bool unifies(
		types::type_t::ref a,
		types::type_t::ref b,
        const ptr<scope_t> &scope);

bool unifies(
		types::type_t::ref a,
		types::type_t::ref b,
		const types::type_t::map &env,
		const types::type_t::map &structural_env);
