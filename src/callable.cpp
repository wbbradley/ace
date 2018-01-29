#include "callable.h"
#include "logger.h"
#include "llvm_utils.h"
#include "type_checker.h"
#include "ast.h"
#include "unification.h"
#include "llvm_types.h"
#include "types.h"
#include "type_instantiation.h"
#include "fitting.h"

bound_var_t::ref make_call_value(
		status_t &status,
		llvm::IRBuilder<> &builder,
		location_t location,
		scope_t::ref scope,
		life_t::ref life,
		bound_var_t::ref function,
		bound_var_t::refs arguments)
{
	return create_callsite(
			status, builder, scope, life, function,
			"temp_call_value", INTERNAL_LOC(), arguments);
}

bound_var_t::ref instantiate_unchecked_fn(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		unchecked_var_t::ref unchecked_fn,
		types::type_t::ref fn_type,
		unification_t unification)
{
	assert(fn_type->ftv_count() == 0 && "we cannot instantiate an abstract function");
	debug_above(5, log(log_info, "we are in scope " c_id("%s"), scope->get_name().c_str()));
	debug_above(5, log(log_info, "it's time to instantiate %s at %s with unified signature %s from %s",
				unchecked_fn->str().c_str(),
				unchecked_fn->get_location().str().c_str(),
				fn_type->str().c_str(),
				unification.str().c_str()));

	/* save and later restore the current branch insertion point */
	llvm::IRBuilderBase::InsertPointGuard ipg(builder);

	/* lifetimes have extents at function boundaries */
	auto life = make_ptr<life_t>(status, lf_function);

	ast::type_product_t::ref type_product = dyncast<const ast::type_product_t>(unchecked_fn->node);

	if (auto function_defn = dyncast<const ast::function_defn_t>(unchecked_fn->node)) {
		/* we shouldn't be here unless we found something to substitute */

		debug_above(4, log(log_info, "building substitution for %s with unification %s",
					function_defn->token.str().c_str(),
					unification.str().c_str()));

		/* create a generic substitution scope with the unification */
		scope_t::ref subst_scope = generic_substitution_scope_t::create(
				status, builder, unchecked_fn->node,
				unchecked_fn->module_scope, unification, fn_type);

		if (auto function = dyncast<const types::type_function_t>(fn_type)) {
			if (auto args = dyncast<const types::type_args_t>(function->args)) {
				bound_type_t::refs bound_args = upsert_bound_types(status,
						builder, subst_scope, args->args);

				if (!!status) {
					std::vector<std::string> names;
					for (auto id : args->names) {
						names.push_back(id->get_name());
					}

					bound_type_t::named_pairs named_args = zip_named_pairs(names, bound_args);

					bound_type_t::ref return_type = upsert_bound_type(status,
							builder, subst_scope, function->return_type);

					if (!!status) {
						/* instantiate the function we want */
						return function_defn->instantiate_with_args_and_return_type(status,
								builder, subst_scope, life, nullptr /*new_scope*/,
								named_args, return_type);
					} else {
						user_message(log_info, status, unchecked_fn->get_location(),
								"while instantiating function %s",
								unchecked_fn->str().c_str());
					}
				}
			} else {
				panic("the arguments are not actually type_args_t");
			}
		} else {
			panic("we should have a product type for our fn_type");
		}
	} else if (type_product != nullptr) {
		ast::item_t::ref node = type_product;

		/* we shouldn't be here unless we found something to substitute */
		debug_above(4, log(log_info, "building substitution for %s",
					node->token.str().c_str()));
		auto unchecked_data_ctor = dyncast<const unchecked_data_ctor_t>(unchecked_fn);
		assert(unchecked_data_ctor != nullptr);
		assert(!unchecked_data_ctor->native);

		/* create a generic substitution scope with the unification */
		scope_t::ref subst_scope = generic_substitution_scope_t::create(
				status, builder, unchecked_fn->node,
				unchecked_fn->module_scope, unification, fn_type);

		if (!!status) {
			types::type_function_t::ref data_ctor_type = dyncast<const types::type_function_t>(
					unchecked_data_ctor->sig->rebind(unification.bindings));
			assert(data_ctor_type != nullptr);
			// if (data_ctor_type->ftv_count() == 0) {
			debug_above(4, log(log_info, "going to bind ctor for %s",
						data_ctor_type->str().c_str()));

			/* instantiate the data ctor we want */
			bound_var_t::ref ctor_fn = bind_ctor_to_scope(
					status, builder, subst_scope,
					unchecked_fn->id, node,
					data_ctor_type);

			if (!!status) {
				/* the ctor should now exist */
				assert(ctor_fn != nullptr);
				return ctor_fn;
			}
		}
	} else {
		panic("we should only have function defn's in unchecked var's, right?");
		return nullptr;
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref check_func_vs_callsite(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		var_t::ref fn,
		types::type_t::ref args,
		types::type_t::ref return_type,
		int &coercions)
{
	assert(!!status);
	if (return_type == nullptr) {
		return_type = type_variable(location);
	}

	unification_t unification = fn->accepts_callsite(builder, scope, args, return_type);
	coercions = unification.coercions;
	if (unification.result) {
		if (auto bound_fn = dyncast<const bound_var_t>(fn)) {
			/* this function has already been bound */
			debug_above(3, log(log_info, "override resolution has chosen %s",
						bound_fn->str().c_str()));
			return bound_fn;
		} else if (auto unchecked_fn = dyncast<const unchecked_var_t>(fn)) {
			/* we're instantiating a template or a forward decl */
			/* we know that fn and args are compatible */
			/* create the new callee signature type for building the generic
			 * substitution scope */
			debug_above(5, log(log_info, "rebinding %s with %s",
						fn->str().c_str(),
						::str(unification.bindings).c_str()));

			types::type_t::ref fn_type = fn->get_type(scope)->rebind(unification.bindings);

			return instantiate_unchecked_fn(status, builder, scope,
					unchecked_fn, fn_type, unification);
		} else {
			panic("unhandled var type");
		}

		assert(!status);
		return nullptr;
	}

	debug_above(4, log(log_info, "fn %s at %s does not match %s because %s",
				fn->str().c_str(),
				location.str().c_str(), 
				args->str().c_str(),
				unification.str().c_str()));

	/* it's possible to exit without finding that the callable matches the
	 * callsite. this is not an error (unless the status indicates so.) */
	return nullptr;
}

bound_var_t::ref maybe_get_callable(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		std::string alias,
		location_t location,
		types::type_t::ref args,
		types::type_t::ref return_type,
		fittings_t &fittings,
		bool check_unchecked)
{
	debug_above(3, log(log_info, "maybe_get_callable(..., scope=%s, alias=%s, args=%s, ...)",
				scope->get_name().c_str(),
				alias.c_str(),
				args->str().c_str()));

    llvm::IRBuilderBase::InsertPointGuard ipg(builder);
	if (!!status) {
#if 0
		if (alias == "mark_allocation") {
			dbg();
		}
#endif

		/* look through the current scope stack and get a callable that is able
		 * to be invoked with the given args */
		var_t::refs fns;
		scope->get_callables(alias, fns, check_unchecked);
		return get_best_fit(status, builder, scope, location, alias, args, return_type, fns);
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref get_callable_from_local_var(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		std::string alias,
		bound_var_t::ref bound_var,
		location_t callsite_location,
		types::type_t::ref args,
		types::type_t::ref return_type)
{
	/* make sure the function is just a function, not a reference to a function */
	auto resolved_bound_var = bound_var->resolve_bound_value(status, builder, scope);

	if (!!status) {
		int coercions = 0;
		bound_var_t::ref callable = check_func_vs_callsite(status, builder,
				scope, callsite_location, resolved_bound_var, args, return_type, coercions);
		if (!!status) {
			if (callable != nullptr) {
				return callable;
			} else {
				user_error(status, callsite_location, "variable " c_id("%s") " is not callable with these arguments or just isn't a function",
						alias.c_str());
				user_info(status, callsite_location, "type of %s is %s",
						alias.c_str(),
						bound_var->type->str().c_str());
			}
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref get_callable(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		std::string alias,
		location_t callsite_location,
		types::type_args_t::ref args,
		types::type_t::ref return_type)
{
	bound_var_t::ref bound_var;
	if (scope->symbol_exists_in_running_scope(alias, bound_var)) {
		return get_callable_from_local_var(status, builder, scope, alias, bound_var,
				callsite_location, args, return_type);
	}

	fittings_t fittings;
	auto callable = maybe_get_callable(status, builder, scope, alias,
			callsite_location, args, return_type, fittings);

	if (!!status) {
		if (callable != nullptr) {
			return callable;
		} else {
			if (fittings.size() == 0) {
				user_error(status, callsite_location,
					   	"no function found with signature " C_TYPE "def" C_RESET " " c_id("%s") "%s %s",
						alias.c_str(),
						args->str().c_str(),
						(return_type != nullptr) ? return_type->str().c_str() : "");
				debug_above(11, log(log_info, "%s", scope->str().c_str()));
			} else {
				std::stringstream ss;
				ss << "unable to resolve overloads for " << C_ID << alias << C_RESET << args->str();
				user_error(status, callsite_location, "%s", ss.str().c_str());

				if (debug_level() >= 0) {
					/* report on the places we tried to look for a match */
					if (fittings.size() > 10) {
						user_message(log_info, status, callsite_location,
								"%d non-matching functions called " c_id("%s")
							   	" found (skipping listing them all)", fittings.size(), alias.c_str());
					} else {
						for (auto &fitting : fittings) {
							ss.str("");
							ss << fitting.fn->type->str() << " did not match";
							user_message(log_info, status, fitting.fn->get_location(), "%s", ss.str().c_str());
						}
					}
				}
			}
			return nullptr;
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref call_program_function(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        std::string function_name,
		location_t callsite_location,
        const bound_var_t::refs var_args,
		types::type_t::ref return_type)
{
    types::type_args_t::ref args = get_args_type(var_args);
	auto program_scope = scope->get_program_scope();

	if (return_type == nullptr) {
		return_type = type_variable(callsite_location);
	}

    /* get or instantiate a function we can call on these arguments */
    bound_var_t::ref function = get_callable(
			status, builder, program_scope, function_name, callsite_location,
			args, return_type);

    if (!!status) {
		return make_call_value(status, builder, callsite_location, scope,
				life, function, var_args);
    } else {
		user_error(status, callsite_location, "failed to resolve function with args: %s",
				::str(var_args).c_str());

		assert(!status);
		return nullptr;
	}
}

