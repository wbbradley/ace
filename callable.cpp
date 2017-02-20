#include "callable.h"
#include "llvm_utils.h"
#include "type_checker.h"
#include "ast.h"
#include "unification.h"
#include "llvm_types.h"
#include "types.h"
#include "type_instantiation.h"

bound_var_t::ref make_call_value(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<const ast::item> callsite,
		scope_t::ref scope,
		life_t::ref life,
		bound_var_t::ref function,
		bound_var_t::refs arguments)
{
	return create_callsite(
			status, builder, scope, life, callsite, function,
			"temp_call_value", INTERNAL_LOC(), arguments);

	assert(!status);
	return nullptr;
}

bound_var_t::ref instantiate_unchecked_fn(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		unchecked_var_t::ref unchecked_fn,
		types::type::ref fn_type,
		unification_t unification)
{
	assert(fn_type->ftv_count() == 0 && "we cannot instantiate an abstract function");
	debug_above(4, log(log_info, "we are in scope " c_id("%s"), scope->get_name().c_str()));
	debug_above(4, log(log_info, "it's time to instantiate %s with unified signature %s from %s",
				unchecked_fn->str().c_str(),
				fn_type->str().c_str(),
				unification.str().c_str()));

	/* save and later restore the current branch insertion point */
	llvm::IRBuilderBase::InsertPointGuard ipg(builder);

	/* lifetimes have extents at function boundaries */
	auto life = make_ptr<life_t>(lf_function);

	ast::type_product::ref type_product = dyncast<const ast::type_product>(unchecked_fn->node);

	if (auto function_defn = dyncast<const ast::function_defn>(unchecked_fn->node)) {
		/* we shouldn't be here unless we found something to substitute */

		debug_above(4, log(log_info, "building substitution for %s with unification %s",
					function_defn->token.str().c_str(),
					unification.str().c_str()));

		/* create a generic substitution scope with the unification */
		scope_t::ref subst_scope = generic_substitution_scope_t::create(
				status, builder, unchecked_fn->node,
				unchecked_fn->module_scope, unification, fn_type);

		if (auto function = dyncast<const types::type_function>(fn_type)) {
			bound_type_t::refs args = upsert_bound_types(status,
					builder, subst_scope, function->args->args);

			if (!!status) {
				bound_type_t::named_pairs named_args = zip_named_pairs(
						get_param_list_decl_variable_names(
							function_defn->decl->param_list_decl),
						args);

				bound_type_t::ref return_type = upsert_bound_type(status,
						builder, subst_scope, function->return_type);

				if (!!status) {
					/* instantiate the function we want */
					return function_defn->instantiate_with_args_and_return_type(status,
							builder, subst_scope, life, nullptr /*new_scope*/,
							function->inbound_context, named_args, return_type);
				}
			}
		} else {
			panic("we should have a product type for our fn_type");
		}
	} else if (type_product != nullptr) {
		ast::item::ref node = type_product;

		/* we shouldn't be here unless we found something to substitute */
		debug_above(4, log(log_info, "building substitution for %s",
					node->token.str().c_str()));
		auto unchecked_data_ctor = dyncast<const unchecked_data_ctor_t>(unchecked_fn);
		assert(unchecked_data_ctor != nullptr);

		/* create a generic substitution scope with the unification */
		scope_t::ref subst_scope = generic_substitution_scope_t::create(
				status, builder, unchecked_fn->node,
				unchecked_fn->module_scope, unification, fn_type);

		if (!!status) {
			types::type_function::ref data_ctor_type = dyncast<const types::type_function>(
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
		const ast::item::ref &callsite,
		var_t::ref fn,
		types::type::ref type_fn_context,
		types::type_args::ref args)
{
	assert(!!status);
	assert(args->ftv_count() == 0 && "how did you get abstract arguments? are you a wizard?");
	unification_t unification = fn->accepts_callsite(builder, scope, type_fn_context, args);
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

			types::type::ref fn_type = fn->get_type(scope)->rebind(unification.bindings);

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
				callsite->str().c_str(), 
				args->str().c_str(),
				unification.str().c_str()));

	/* it's possible to exit without finding that the callable matches the
	 * callsite. this is not an error (unless the status indicates so.) */
	return nullptr;
}

bool function_exists_in(var_t::ref fn, std::list<bound_var_t::ref> callables) {
    for (auto callable : callables) {
        if (callable->get_location() == fn->get_location()) {
            return true;
        }
    }
    return false;
}

bound_var_t::ref maybe_get_callable(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		atom alias,
		const ptr<const ast::item> &callsite,
		types::type::ref type_fn_context,
		types::type_args::ref args,
		var_t::refs &fns)
{
	debug_above(3, log(log_info, "maybe_get_callable(..., scope=%s, alias=%s, type_fn_context=%s, args=%s, ...)",
				scope->get_name().c_str(),
				alias.c_str(),
				type_fn_context->str().c_str(),
				args->str().c_str()));

    llvm::IRBuilderBase::InsertPointGuard ipg(builder);
    std::list<bound_var_t::ref> callables;
	if (!!status) {
		/* look through the current scope stack and get a callable that is able
		 * to be invoked with the given args */
		scope->get_callables(alias, fns);
		for (auto &fn : fns) {
            if (function_exists_in(fn, callables)) {
                /* we've already found a matching version of this function,
                 * let's not bind it again */
				debug_above(7, log(log_info,
							"skipping checking %s because we've already got a matched version of that function",
							fn->str().c_str()));
				continue;
            }
			bound_var_t::ref callable = check_func_vs_callsite(status, builder,
					scope, callsite, fn, type_fn_context, args);

			if (!status) {
				assert(callable == nullptr);
				return nullptr;
			} else if (callable != nullptr) {
				callables.push_front(callable);
			}
		}

        if (!!status) {
            if (callables.size() == 1) {
                return callables.front();
            } else if (callables.size() == 0) {
                return nullptr;
            } else {
				user_error(status, callsite->get_location(),
						"multiple matching overloads found for %s at %s",
						alias.c_str(), callsite->str().c_str());
                for (auto callable :callables) {
                    user_message(log_info, status, callable->get_location(),
						   	"matching overload : %s",
                            callable->type->get_type()->str().c_str());
                }
            }
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
		types::type::ref outbound_context,
		types::type_args::ref args)
{
	var_t::refs fns;
	// TODO: potentially allow fake calling contexts by adding syntax to the
	// callsite
	auto callable = maybe_get_callable(status, builder, scope, alias, callsite,
			outbound_context, args, fns);

	if (!!status) {
		if (callable != nullptr) {
			return callable;
		} else {
			if (fns.size() == 0) {
                user_error(status, *callsite, "no function found named " c_id("%s") " for callsite %s with %s in " c_id("%s"),
                        alias.c_str(), callsite->str().c_str(),
                        args->str().c_str(),
                        scope->get_name().c_str());
				debug_above(11, log(log_info, "%s", scope->str().c_str()));
			} else {
				std::stringstream ss;
				ss << "unable to resolve overloads for " << callsite->str() << args->str();
				ss << " from context " << outbound_context->str();
				user_error(status, *callsite, "%s", ss.str().c_str());

				if (debug_level() >= 0) {
					/* report on the places we tried to look for a match */
					for (auto &fn : fns) {
						ss.str("");
						ss << fn->get_type(scope)->str() << " did not match";
						user_message(log_info, status, fn->get_location(), "%s", ss.str().c_str());
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
        atom function_name,
        const ptr<const ast::item> &callsite,
        const bound_var_t::refs var_args)
{
    types::type_args::ref args = get_args_type(var_args);
	auto program_scope = scope->get_program_scope();
    /* get or instantiate a function we can call on these arguments */
    bound_var_t::ref function = get_callable(
			status, builder, program_scope, function_name, callsite,
			program_scope->get_inbound_context(), args);

    if (!!status) {
		return make_call_value(status, builder, callsite, scope, life, function,
				var_args);
    } else {
		user_error(status, callsite->get_location(), "failed to resolve function with args: %s",
				::str(var_args).c_str());

		assert(!status);
		return nullptr;
	}
}

