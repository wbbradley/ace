#pragma once
#include "zion.h"
#include "status.h"
#include "bound_var.h"
#include "scopes.h"

namespace ast {
    struct item;
};

struct can_reference_overloads_t {
	virtual ~can_reference_overloads_t() throw() {}
	virtual bound_var_t::ref resolve_overrides(
			status_t &status,
			llvm::IRBuilder<> &builder,
			scope_t::ref scope,
			const ptr<const ast::item> &obj,
			const bound_type_t::refs &args) const = 0;
};

bound_var_t::ref make_call_value(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<const ast::item> callsite,
		scope_t::ref scope,
		bound_var_t::ref function,
		bound_var_t::refs arguments);

bound_var_t::ref get_callable(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		atom alias,
		const ptr<const ast::item> &obj,
		types::type::ref outbound_context,
		types::type_args::ref sig_args);

/* maybe_get_callable is supposed to be more lenient and not cause errors,
 * however it may go off and type check potential unifications of other generic
 * functions and cause user errors */
bound_var_t::ref maybe_get_callable(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		atom alias,
		const ptr<const ast::item> &callsite,
		types::type::ref type_fn_context,
		types::type_args::ref sig_args,
		var_t::refs &fns);

bound_var_t::ref call_program_function(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        atom function_name,
        const ptr<const ast::item> &callsite,
        const bound_var_t::refs args);
