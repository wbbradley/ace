#pragma once
#include "zion.h"
#include "user_error.h"
#include "bound_var.h"
#include "scopes.h"
#include "life.h"
#include "fitting.h"

namespace ast {
    struct item_t;
	struct block_t;
};

struct can_reference_overloads_t {
	virtual ~can_reference_overloads_t() throw() {}
	virtual bound_var_t::ref resolve_overrides(
			llvm::IRBuilder<> &builder,
			scope_t::ref scope,
			life_t::ref,
			const ptr<const ast::item_t> &obj,
			const bound_type_t::refs &args) const = 0;
};

bound_var_t::ref make_call_value(
		llvm::IRBuilder<> &builder,
		location_t location,
		scope_t::ref scope,
		life_t::ref life,
		bound_var_t::ref function,
		bound_var_t::refs arguments);

bound_var_t::ref get_callable(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		std::string alias,
		location_t callsite_location,
		types::type_args_t::ref sig_args,
		types::type_t::ref return_type);

/* maybe_get_callable is supposed to be more lenient and not cause errors,
 * however it may go off and type check potential unifications of other generic
 * functions and cause user errors */
bound_var_t::ref maybe_get_callable(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		std::string alias,
		location_t location,
		types::type_t::ref type_args,
		types::type_t::ref return_type,
		fittings_t &fittings,
		bool check_unchecked=true,
		bool allow_coercions=true);

bound_var_t::ref call_program_function(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        std::string function_name,
		location_t location,
        const bound_var_t::refs args,
		types::type_t::ref return_type=nullptr);

bound_var_t::ref call_module_function(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        std::string function_name,
		location_t callsite_location,
        const bound_var_t::refs var_args,
		types::type_t::ref return_type=nullptr);
bound_var_t::ref check_func_vs_callsite(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		var_t::ref fn,
		types::type_t::ref args,
		types::type_t::ref return_type,
		int &coercions);
bound_var_t::ref instantiate_function_with_args_and_return_type(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		token_t name_token,
		bool as_closure,
		identifier::ref extends_module,
		runnable_scope_t::ref *new_scope,
		types::type_t::ref type_constraints,
		bound_type_t::named_pairs args,
		bound_type_t::ref return_type,
		types::type_function_t::ref fn_type,
		ptr<const ast::block_t> block);
bound_var_t::ref instantiate_unchecked_fn(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		unchecked_var_t::ref unchecked_fn,
		types::type_function_t::ref fn_type,
		const unification_t *unification);
