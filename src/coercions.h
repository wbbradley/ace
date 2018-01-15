llvm::Value *coerce_value(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		types::type_t::ref lhs_type,
		bound_var_t::ref rhs);
std::vector<llvm::Value *> get_llvm_values(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		ptr<const types::type_args_t> type_args,
		const bound_var_t::refs &vars);
