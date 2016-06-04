#include "callable.h"
#include "llvm_utils.h"
#include "type_checker.h"
#include "ast.h"
#include "unification.h"
#include "llvm_types.h"
#include "types.h"

bound_var_t::ref make_call_value(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<const ast::item> callsite,
		scope_t::ref scope,
		bound_var_t::ref function,
		bound_var_t::refs arguments)
{
	return create_callsite(
			status, builder, scope, callsite, function,
			"temp_call_value", INTERNAL_LOC(), arguments);

	assert(!status);
	return nullptr;
}

bound_var_t::ref check_func_vs_callsite(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		const ast::item::ref &callsite,
		var_t::ref fn,
		types::term::ref args)
{
	if (auto unification = fn->accepts_callsite(scope, args)) {
		if (auto bound_fn = dyncast<const bound_var_t>(fn)) {
			/* this function has already been bound */
			log(log_info, "override resolution has chosen %s", bound_fn->str().c_str());
			return bound_fn;
		} else if (auto unchecked_fn = dyncast<const unchecked_var_t>(fn)) {
			/* we're instantiating a template or a forward decl */
			/* we know that fn and sig_args are compatible */
			log(log_info, "it's time to instantiate %s with unification %s",
					unchecked_fn->str().c_str(),
					unification->str().c_str());

			types::term::ref fn_sig = fn->get_term();
			assert(false);
			if (false) { // TODO: figure out whether we care about "bound" things anymore: !fn_sig->is_generic()) {
				/* this is a forward decl, we just need to recurse into
				 * resolving it */
				if (auto fn_defn = dyncast<const ast::function_defn>(unchecked_fn->node)) {
					auto module_scope = unchecked_fn->module_scope;

					/* save and later restore the current branch insertion point */
					llvm::IRBuilderBase::InsertPointGuard ipg(builder);

					log(log_info, "recursing into a forward declaration for %s", fn_defn->token.str().c_str());
					return fn_defn->resolve_instantiation(status, builder,
							module_scope, nullptr, nullptr);
				} else {
					panic("strange non-function var");
				}
			} else {
				/* save and later restore the current branch insertion point */
				llvm::IRBuilderBase::InsertPointGuard ipg(builder);

				if (auto function_defn = dyncast<const ast::function_defn>(unchecked_fn->node)) {
					/* we shouldn't be here unless we found something to substitute */

					log(log_info, "building substitution for %s", function_defn->token.str().c_str());

					/* create a generic substitution scope with the unification */
					scope_t::ref subst_scope = generic_substitution_scope_t::create(
							status, builder, unchecked_fn->node,
							unchecked_fn->module_scope, unification);

					/* instantiate the function we want */
					return function_defn->resolve_instantiation(status, builder,
							subst_scope, nullptr, nullptr);
				} else if (auto data_ctor = dyncast<const ast::data_ctor>(unchecked_fn->node)) {
					/* we shouldn't be here unless we found something to substitute */

					log(log_info, "building substitution for %s", data_ctor->token.str().c_str());

					/* create a generic substitution scope with the unification */
					scope_t::ref subst_scope = generic_substitution_scope_t::create_for_types(
							status, builder, unchecked_fn->node,
							unchecked_fn->module_scope, unification,
							data_ctor->type_variables);

					/* instantiate the data ctor we want */
					bool fully_bound = true;
					bound_var_t::ref ctor_fn = bind_ctor_to_scope(
							status, builder, subst_scope, data_ctor, fully_bound);

					if (!!status) {
						/* the ctor should now exist */
						assert_implies(fully_bound, ctor_fn != nullptr);

						if (ctor_fn == nullptr) {
							user_error(status, *callsite,
									"unable to fully bind data constructor");
						} else {
							return ctor_fn;
						}
					}
				} else {
					panic("we should only have function defn's in unchecked var's, right?");
					return nullptr;
				}
			}
		} else {
			panic("unhandled var type");
		}
	}

	/* it's possible to exit without finding that the callable matches the
	 * callsite. this is not an error (unless the status indicates so.) */
	return nullptr;
}

bound_var_t::ref maybe_get_callable(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		atom alias,
		const ptr<const ast::item> &callsite,
		types::term::ref args,
		var_t::refs &fns)
{
    llvm::IRBuilderBase::InsertPointGuard ipg(builder);

	/* look through the current scope stack and get a callable that is able to
	 * be invoked with the given args */
	scope->get_callables(alias, fns);
	for (auto &fn : fns) {
		bound_var_t::ref callable = check_func_vs_callsite(status, builder,
				scope, callsite, fn, args);

		if (!status) {
			assert(callable == nullptr);
			return nullptr;
		} else if (callable != nullptr) {
			return callable;
		}
	}
	return nullptr;
}

bound_var_t::ref get_callable(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		atom alias,
		const ptr<const ast::item> &callsite,
		types::term::ref args)
{
	var_t::refs fns;
	auto callable = maybe_get_callable(status, builder, scope, alias, callsite,
			args, fns);
	if (!!status) {
		if (callable != nullptr) {
			return callable;
		} else {
			if (fns.size() == 0) {
				user_error(status, *callsite, "no function found named " c_id("%s"), alias.c_str());
			} else {
				user_error(status, *callsite,
						"unable to resolve overloads for %s. arguments are %s.",
						callsite->str().c_str(),
						args->str().c_str());

				for (auto &fn : fns) {
					user_error(status, fn->get_location(), "%s",
							fn->str().c_str());
				}
			}
			return nullptr;
		}
	} else {
		log(log_warning, "failure when calling get_callable. probably need to have harder checks on input");
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref call_program_function(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        atom function_name,
        const ptr<const ast::item> &callsite,
        const bound_var_t::refs var_args)
{
    types::term::ref args = get_args_term(var_args);

    /* get or instantiate a function we can call on these arguments */
    bound_var_t::ref function = get_callable(
			status, builder, scope->get_program_scope(), function_name,
			callsite, args);

    if (!!status) {
		return make_call_value(status, builder, callsite, scope, function,
				var_args);
    } else {
		assert(!status);
		return nullptr;
	}
}

