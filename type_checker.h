#pragma once
#include "ast_decls.h"
#include "scopes.h"
#include "life.h"
#include <map>
#include "llvm_zion.h"

struct status_t;
struct compiler_t;

void type_check_program(
        status_t &status,
		llvm::IRBuilder<> &builder,
		const ast::program_t &obj,
		compiler_t &compiler);

bool is_function_defn_generic(status_t &status, llvm::IRBuilder<> &builder, scope_t::ref scope, const ast::function_defn_t &obj);
atom::many get_param_list_decl_variable_names(ptr<const ast::param_list_decl_t> obj);
bound_type_t::named_pairs zip_named_pairs(atom::many names, bound_type_t::refs args);
bound_var_t::ref call_typeid(
		status_t &status,
		scope_t::ref scope,
		life_t::ref life,
		ptr<const ast::item_t> callsite,
		identifier::ref id,
		llvm::IRBuilder<> &builder,
		bound_var_t::ref resolved_value);
