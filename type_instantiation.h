#pragma once
#include "zion.h"
#include "types.h"
#include "scopes.h"

types::term::ref register_data_ctor(
		status_t &status,
		llvm::IRBuilder<> &builder,
		atom::many type_variables,
		scope_t::ref scope,
		ptr<const ast::item> node,
		types::term::refs dimensions,
		identifier::ref id,
		identifier::ref supertype_id);

bound_var_t::ref bind_ctor_to_scope(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		identifier::ref id,
		ptr<const ast::item> data_ctor,
		types::type::ref data_ctor_sig);
