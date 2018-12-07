#pragma once
#include "zion.h"
#include "user_error.h"
#include "bound_var.h"
#include "scopes.h"
#include "life.h"
#include "fitting.h"
#include "binding.h"
#include <set>

namespace ast {
    struct item_t;
	struct block_t;
};

struct delegate_t;

struct can_reference_overloads_t {
	virtual ~can_reference_overloads_t() throw() {}
	virtual var_t::ref resolve_overrides(
			delegate_t &delegate,
			scope_t::ref scope,
			life_t::ref,
			const std::shared_ptr<const ast::item_t> &obj,
			const bound_type_t::refs &args,
			types::type_t::ref return_type,
			bool *returns) const = 0;
	virtual types::type_function_t::ref resolve_arg_types_from_overrides(
			scope_t::ref scope,
		   	location_t location,
		   	types::type_t::refs args,
		   	types::type_t::ref return_type) const = 0;
};

var_t::ref make_call_value(
		delegate_t &delegate,
		location_t location,
		scope_t::ref scope,
		life_t::ref life,
		var_t::ref function,
		var_t::refs arguments);

var_t::ref get_callable(
		delegate_t &delegate,
		scope_t::ref scope,
		std::string alias,
		location_t callsite_location,
		types::type_args_t::ref args,
		types::type_t::ref return_type);

/* maybe_get_callable is supposed to be more lenient and not cause errors,
 * however it may go off and type check potential unifications of other generic
 * functions and cause user errors */
var_t::ref maybe_get_callable(
		delegate_t &delegate,
		scope_t::ref scope,
		std::string alias,
		location_t location,
		types::type_t::ref type_args,
		types::type_t::ref return_type,
		var_t::refs &fns,
		fittings_t &fittings,
		bool check_unchecked=true,
		bool allow_coercions=true);

var_t::ref call_program_function(
		delegate_t &delegate,
        scope_t::ref scope,
		life_t::ref life,
        std::string function_name,
		location_t location,
        const var_t::refs args,
		types::type_t::ref return_type=nullptr);

var_t::ref call_module_function(
        delegate_t &delegate,
        scope_t::ref scope,
		life_t::ref life,
        std::string function_name,
		location_t callsite_location,
        var_t::refs var_args,
		types::type_t::ref return_type=nullptr);
var_t::ref check_bound_func_vs_callsite(
		delegate_t &delegate,
		scope_t::ref scope,
		location_t location,
		var_t::ref fn,
		types::type_t::ref args,
		types::type_t::ref return_type,
		int &coercions,
		bindings_set_t &checked_bindings);
types::type_function_t::ref check_func_type_vs_callsite(
		scope_t::ref scope,
		location_t location,
		var_t::ref fn,
		types::type_t::ref args,
		types::type_t::ref return_type);
void check_func_vs_callsite(
		scope_t::ref scope,
		location_t location,
		var_t::ref fn,
		types::type_t::ref args,
		types::type_t::ref return_type,
		int &coercions,
		std::function<void (scope_t::ref, var_t::ref, types::type_t::map const &)> &callback);
bound_var_t::ref instantiate_function_with_args_and_return_type(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		token_t name_token,
		bool as_closure,
        bool needs_type_fixup,
		identifier::ref extends_module,
		runnable_scope_t::ref *new_scope,
		types::type_t::ref type_constraints,
		bound_type_t::named_pairs args,
		bound_type_t::ref return_type,
		types::type_function_t::ref fn_type,
		std::shared_ptr<const ast::block_t> block);
bound_var_t::ref instantiate_unchecked_fn(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		unchecked_var_t::ref unchecked_fn,
		types::type_function_t::ref fn_type,
		const types::type_t::map &bindings);
