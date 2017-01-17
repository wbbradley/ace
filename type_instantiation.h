#pragma once
#include "zion.h"
#include "types.h"
#include "scopes.h"

types::type::ref register_data_ctor(
		status_t &status,
		llvm::IRBuilder<> &builder,
		types::type::ref type,
		identifier::refs type_variables,
		scope_t::ref scope,
		ptr<const ast::item> node,
		identifier::ref id_,
		identifier::ref supertype_id);

bound_var_t::ref bind_ctor_to_scope(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		identifier::ref id,
		ptr<const ast::item> node,
		types::type::ref fn_type);

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
