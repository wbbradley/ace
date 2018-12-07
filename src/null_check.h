#pragma once
#include "ast_decls.h"
#include "scopes.h"
#include "life.h"
#include "llvm_zion.h"

struct delegate_t;

enum null_check_kind_t {
	nck_is_non_null,
	nck_is_null,
};

var_t::ref get_null(
		delegate_t &delegate,
        scope_t::ref scope,
		location_t location);

var_t::ref resolve_null_check(
		delegate_t &delegate,
		runnable_scope_t::ref scope,
		life_t::ref life,
		location_t location,
		std::shared_ptr<const ast::item_t> node,
		var_t::ref value,
		null_check_kind_t nck,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false);
