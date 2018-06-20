#include "zion.h"
#include "scopes.h"
#include "bound_var.h"
#include "ast.h"

bound_type_t::refs upsert_bound_types(
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type_t::refs types);

bound_type_t::ref upsert_bound_type(
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type_t::ref type);

bound_type_t::ref upsert_bound_type(
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type_t::ref type);

std::pair<bound_var_t::ref, bound_type_t::ref> upsert_tuple_ctor(
		
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type_tuple_t::ref tuple_type,
		const ptr<const ast::item_t> &node);

bound_var_t::ref upsert_tagged_tuple_ctor(
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		identifier::ref id,
		std::string ctor_name,
		location_t location,
		types::type_t::ref data_type,
		types::type_t::ref return_type);

bound_var_t::ref get_or_create_tuple_ctor(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::ref bound_data_type,
		bound_type_t::ref bound_return_type,
		identifier::ref id,
		std::string ctor_name,
		location_t location);

bound_type_t::ref get_function_return_type(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::ref function_type);

bound_var_t::ref upsert_type_info(
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		std::string name,
		location_t location,
		bound_type_t::ref data_type,
		bound_type_t::refs args,
		bound_var_t::ref dtor_fn,
		bound_var_t::ref mark_fn);
llvm::Value *llvm_make_gep(llvm::IRBuilder<> &builder, llvm::Value *llvm_value, int index, bool managed);
