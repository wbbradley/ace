#pragma once
#include "ast_decls.h"
#include "scopes.h"
#include <unordered_map>
#include <map>
#include "llvm_zion.h"

struct status_t;
struct compiler;

status_t type_check_program(
		llvm::IRBuilder<> &builder,
		const ast::program &obj,
		compiler &compiler);

status_t type_check_binary_operator(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		ptr<const ast::expression> lhs,
		ptr<const ast::expression> rhs,
		const ast::expression &obj,
		ptr<bound_var_t> &variable);

status_t make_temp_variable(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		const ast::item &obj,
		types::type::ref type,
		llvm::Value *llvm_value,
		ptr<bound_var_t> &variable);

typedef atom::set bound_type_context_t;

bool is_function_defn_generic(status_t &status, llvm::IRBuilder<> &builder, scope_t::ref scope, const ast::function_defn &obj);
atom::many get_param_list_decl_variable_names(ptr<const ast::param_list_decl> obj);
bound_type_t::named_pairs zip_named_pairs(atom::many names, bound_type_t::refs args);
bound_var_t::ref call_typeid(
		status_t &status,
		scope_t::ref scope,
		ptr<const ast::item> callsite,
		identifier::ref id,
		llvm::IRBuilder<> &builder,
		bound_var_t::ref resolved_value);
