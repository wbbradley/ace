#pragma once
#include "ast_decls.h"
#include "scopes.h"
#include "life.h"
#include "llvm_zion.h"

struct status_t;

enum null_check_kind_t {
	nck_is_non_null,
	nck_is_null,
};

bound_var_t::ref resolve_null_check(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		const std::vector<ptr<ast::expression_t>> &params,
		null_check_kind_t nck);

bound_var_t::ref resolve_null_check(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		bound_var_t::ref value,
		null_check_kind_t nck);
