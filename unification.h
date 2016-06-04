#pragma once
#include "ptr.h"
#include "assert.h"
#include <memory>
#include <vector>
#include <map>
#include "types.h"

struct unification_t {
	typedef ptr<const unification_t> ref;

	unification_t() = delete;
	unification_t(
			types::term::ref unified_type,
		   	types::term::ref lhs,
		   	types::term::ref rhs) :
	   	//unified_type(unified_type),
		lhs_initial(lhs),
	   	rhs_initial(rhs) {}

	std::string str() const;

	/* lhs is the potentially generic term of a function */
	types::term::ref lhs_initial;

	/* rhs is the callsite which should be fully bound */
	types::term::ref rhs_initial;

	/* the bound types which we end up with after the unification process */
	types::term::map bindings;
};

unification_t::ref unify(
		status_t &status,
		const types::term::map &env,
		const types::term::ref &a,
		const types::term::ref &b);


#if 0
template <typename T>
term_t::ref upsert_var_name(term_t::map &map, atom name) {
	auto iter = map.find(name);
	if (iter != map.end()) {
		return iter->second;
	} else {
		auto term = T::create(name);
		map[name] = term;
		return term;
	}
}

template <typename T, typename ...Args>
term_t::ref upsert_var_name(term_t::map &map, atom name, Args... args) {
	auto iter = map.find(name);
	if (iter != map.end()) {
		return iter->second;
	} else {
		auto term = T::create(name, args...);
		map[name] = term;
		return term;
	}
}
#endif
