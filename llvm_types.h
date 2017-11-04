#include "zion.h"
#include "scopes.h"
#include "bound_var.h"
#include "ast.h"

bound_type_t::refs upsert_bound_types(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type_t::refs types);

bound_type_t::ref upsert_bound_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type_t::ref type);

bound_type_t::ref upsert_bound_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type_t::ref type);

std::pair<bound_var_t::ref, bound_type_t::ref> instantiate_tuple_ctor(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		bound_type_t::refs args,
		identifier::ref id,
		const ptr<const ast::item_t> &node);

std::pair<bound_var_t::ref, bound_type_t::ref> instantiate_tagged_tuple_ctor(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		identifier::ref id,
		const ptr<const ast::item_t> &node,
		types::type_t::ref type);

bound_var_t::ref get_or_create_tuple_ctor(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::ref data_type,
		identifier::ref id,
		const ast::item_t::ref &node);

#if 0
bound_var_t::ref call_const_subscript_operator(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		const ast::item_t::ref &node,
		bound_var_t::ref lhs,
		identifier::ref index_id,
		uint64_t subscript_index);
#endif

bound_type_t::ref get_function_return_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::ref function_type);

bound_var_t::ref upsert_type_info(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		atom name,
		location_t location,
		bound_type_t::ref data_type,
		bound_type_t::refs args,
		bound_var_t::ref dtor_fn,
		bound_var_t::ref mark_fn);
llvm::Value *llvm_make_gep(llvm::IRBuilder<> &builder, llvm::Value *llvm_value, int index, bool managed);
