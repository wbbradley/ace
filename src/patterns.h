#pragma once

bound_var_t::ref gen_type_check(
		llvm::IRBuilder<> &builder,
		ast::item_t::ref node,
		scope_t::ref scope,
		life_t::ref life,
		identifier::ref value_name,
		bound_var_t::ref value,
		bound_type_t::ref bound_type,
		runnable_scope_t::ref *new_scope);
