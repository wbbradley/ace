#pragma once
#include "zion.h"
#include "types.h"
#include "scopes.h"

bound_var_t::ref bind_ctor_to_scope(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		identifier::ref id,
		ptr<const ast::item> node,
		types::type_function::ref function);

void resolve_type_ref_params(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		types::type::refs type_args,
		bound_type_t::refs &args);

void create_supertype_relationship(
		status_t &status,
	   	types::type::ref subtype,
		identifier::ref subtype_id,
		identifier::ref supertype_id,
		identifier::refs type_variables,
	   	scope_t::ref scope,
		std::list<identifier::ref> &lambda_vars,
		atom::set &generics);
