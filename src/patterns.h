#pragma once

bound_var_t::ref cast_data_type_to_ctor_struct(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		location_t value_location,
		bound_var_t::ref input_value,
		token_t ctor_name);
