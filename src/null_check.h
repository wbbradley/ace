#pragma once
#include "ast_decls.h"
#include "scopes.h"
#include "life.h"
#include "llvm_zion.h"

enum null_check_kind_t {
	nck_is_non_null,
	nck_is_null,
};

bound_var_t::ref get_null(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		location_t location);

bound_var_t::ref resolve_null_check(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		location_t location,
		ptr<const ast::item_t> node,
		bound_var_t::ref value,
		null_check_kind_t nck,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false);
