#pragma once
#include "zion.h"
#include "types.h"
#include "scopes.h"

types::term::ref register_data_ctor(
		status_t &status,
		llvm::IRBuilder<> &builder,
		identifier::refs type_variables,
		scope_t::ref scope,
		ptr<const ast::item> node,
		types::term::refs dimensions,
		atom::map<int> member_index,
		identifier::ref id,
		identifier::ref supertype_id);

bound_var_t::ref bind_ctor_to_scope(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		identifier::ref id,
		ptr<const ast::item> node,
		types::type::refs args_types,
		types::type::ref return_type,
		atom::map<int> member_index);

void resolve_type_ref_params(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		types::type::refs type_args,
		bound_type_t::refs &args);

void create_supertype_relationship(
		status_t &status,
	   	types::term::ref subtype_term,
		identifier::ref subtype_id,
		identifier::ref supertype_id,
		identifier::refs type_variables,
	   	scope_t::ref scope,
		std::list<identifier::ref> &lambda_vars,
		atom::set &generics);
