#pragma once
#include "zion.h"
#include "llvm_zion.h"
#include "bound_type.h"
#include "types.h"
#include "scopes.h"
#include "var.h"
#include "unchecked_var.h"
#include "life.h"
#include "ast_decls.h"


struct delegate_t {
	delegate_t(llvm::IRBuilder<> &builder, bool use_llvm=false) : 
		builder(builder),
		use_llvm(use_llvm)
	{}

	delegate_t() = delete;
	delegate_t get_type_delegate() const;
	bound_type_t::ref upsert_bound_type(scope_t::ref scope, types::type_t::ref type);
	var_t::ref refine_var_type(
			scope_t::ref scope,
			location_t location,
			types::type_t::ref refined_type,
			var_t::ref value,
			identifier::ref id);
	var_t::ref instantiate_unchecked_fn(
			scope_t::ref scope,
			unchecked_var_t::ref unchecked_fn,
			types::type_function_t::ref fn_type,
			const types::type_t::map &bindings);
	var_t::ref dereferencing_load(
			var_t::ref value,
			scope_t::ref scope);
	var_t::ref create_callsite(
			scope_t::ref scope,
			life_t::ref life,
			var_t::ref function_,
			std::string name,
			const location_t &location,
			var_t::refs arguments_);
	var_t::ref resolve_pointer_array_index(
			scope_t::ref scope,
			life_t::ref life,
			types::type_t::ref element_type,
			std::shared_ptr<const ast::expression_t> index,
			var_t::ref index_val,
			std::shared_ptr<const ast::expression_t> lhs,
			var_t::ref lhs_val,
			bool as_ref,
			std::shared_ptr<const ast::expression_t> rhs);
	llvm::IRBuilder<> &get_builder(location_t location);
	std::function<void ()> get_ip_restorer();
	bound_type_t::ref get_bound_type(scope_t::ref scope, var_t::ref val);

private:
	llvm::IRBuilder<> &builder;
public:
	const bool use_llvm;
};
