#pragma once

#include "bound_var.h"

struct fitting_t {
	var_t::ref var_fn;
	var_t::ref fn;
	int coercions = 0;

	fitting_t() {}
	fitting_t(var_t::ref var_fn, var_t::ref fn, int coercions) :
		var_fn(var_fn),
		fn(fn),
		coercions(coercions)
	{
		assert(dyncast<const bound_var_t>(fn) != nullptr);
	}
};

struct fittings_t {
	std::vector<fitting_t> fittings;
	void push_back(const fitting_t &fitting);
	void clear();
	void reserve(size_t i);
	bool contains(var_t::ref fn) const;
	size_t size() const;

	var_t::ref get_best_fitting(
			location_t location,
			std::string alias, 
			types::type_t::ref args,
			types::type_t::ref return_type);
};


var_t::ref get_best_fit(
		delegate_t &delegate,
		scope_t::ref scope,
		location_t location,
		std::string alias,
		types::type_t::ref args,
		types::type_t::ref return_type,
		var_t::refs &fns,
		fittings_t &fittings,
		bool allow_coercions);
