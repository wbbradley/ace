#pragma once

struct fitting_t {
	var_t::ref var_fn;
	bound_var_t::ref fn;
	int coercions;
};

struct fittings_t {
	std::vector<fitting_t> fittings;
	void push_back(const fitting_t &fitting);
	void clear();
	void reserve(size_t i);
	bool contains(bound_var_t::ref fn) const;
	size_t size() const;

	bound_var_t::ref get_best_fitting(
			location_t location,
			std::string alias, 
			types::type_t::ref args,
			types::type_t::ref return_type);
};


bound_var_t::ref get_best_fit(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		std::string alias,
		types::type_t::ref args,
		types::type_t::ref return_type,
		var_t::refs &fns,
		fittings_t &fittings,
		bool allow_coercions);
