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
