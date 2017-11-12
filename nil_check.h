#pragma once
#include "ast_decls.h"
#include "scopes.h"
#include "life.h"
#include "llvm_zion.h"

struct status_t;

enum nil_check_kind_t {
	nck_is_non_nil,
	nck_is_nil,
};

bound_var_t::ref resolve_nil_check(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		const std::vector<ptr<ast::expression_t>> &params,
		nil_check_kind_t nck);

bound_var_t::ref resolve_nil_check(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		bound_var_t::ref value,
		nil_check_kind_t nck);
