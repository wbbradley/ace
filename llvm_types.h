#include "zion.h"
#include "scopes.h"
#include "bound_var.h"
#include "ast.h"

bound_type_t::refs create_bound_types_from_args(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type::ref args_type);

bound_type_t::ref upsert_bound_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type::ref type);

bound_type_t::ref upsert_bound_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::term::ref term);

std::pair<bound_var_t::ref, bound_type_t::ref> instantiate_tuple_ctor(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		bound_type_t::refs args,
		atom name,
		const location &location,
		const ptr<const ast::item> &node);

std::pair<bound_var_t::ref, bound_type_t::ref> instantiate_tagged_tuple_ctor(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		bound_type_t::refs args,
		atom name,
		const location &location,
		const ptr<const ast::item> &node,
		types::type::ref data_ctor_sig);

std::pair<bound_var_t::ref, bound_type_t::ref> instantiate_struct_ctor(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		bound_type_t::ref struct_type,
		atom name,
		const location &location,
		const ptr<const ast::item> &node);

bound_type_t::ref get_or_create_tuple_type(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		atom name,
		bound_type_t::refs args,
		const ast::item::ref &node);

bound_var_t::ref get_or_create_tuple_ctor(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::refs args,
		bound_type_t::ref data_type,
		atom name,
		const location &location,
		const ast::item::ref &node);

bound_var_t::ref call_const_subscript_operator(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		const ast::item::ref &node,
		bound_var_t::ref lhs,
		int subscript_index);

bound_type_t::ref get_function_return_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ast::item &obj,
		scope_t::ref scope,
		bound_type_t::ref function_type);

bound_var_t::ref bind_ctor_to_scope(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		ast::data_ctor::ref data_ctor,
		types::type::ref data_ctor_sig);
