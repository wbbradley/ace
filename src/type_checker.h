#pragma once
#include "ast_decls.h"
#include "scopes.h"
#include "life.h"
#include <map>
#include "llvm_zion.h"

struct compiler_t;

void type_check_program(
		llvm::IRBuilder<> &builder,
		const ast::program_t &obj,
		compiler_t &compiler);

bool is_function_defn_generic(scope_t::ref scope, const ast::function_defn_t &obj);
std::vector<std::string> get_param_list_decl_variable_names(ptr<const ast::param_list_decl_t> obj);
bound_type_t::named_pairs zip_named_pairs(std::vector<std::string> names, bound_type_t::refs args);
bound_var_t::ref call_get_ctor_id(
		scope_t::ref scope,
		life_t::ref life,
		ptr<const ast::item_t> callsite,
		identifier::ref id,
		llvm::IRBuilder<> &builder,
		bound_var_t::ref resolved_value);
bound_var_t::ref extract_member_variable(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		bound_var_t::ref bound_var,
		std::string member_name,
		bool as_ref,
		types::type_t::ref expected_type);
bound_var_t::ref extract_member_by_index(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		bound_var_t::ref bound_var,
		bound_type_t::ref bound_obj_type,
		int index,
		std::string member_name,
		bool as_ref);
ptr<ast::callsite_expr_t> expand_callsite_string_literal(
		token_t token,
		std::string module,
		std::string function_name,
		std::string param);
void resolve_assert_macro(
		llvm::IRBuilder<> &builder, 
		scope_t::ref scope, 
		life_t::ref life,
		token_t token,
		ptr<ast::expression_t> condition,
		runnable_scope_t::ref *new_scope);
int64_t parse_int_value(token_t token);
void destructure_function_decl(
        llvm::IRBuilder<> &builder,
        const ast::function_decl_t &decl,
        scope_t::ref scope,
        types::type_t::ref &type_constraints,
        bool as_closure,
		bool is_extern_function,
        bool &needs_type_fixup,
        bound_type_t::named_pairs &params,
        bound_type_t::ref &return_type,
        types::type_function_t::ref &function_type,
		types::type_t::ref expected_type);
