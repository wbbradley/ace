#pragma once
#include "zion.h"
#include "types.h"
#include "scopes.h"

bound_var_t::ref bind_ctor_to_scope(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		identifier::ref id,
		location_t location,
		types::type_function_t::ref function);
