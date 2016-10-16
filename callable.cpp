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
		bound_var_t::ref function,
		bound_var_t::refs arguments)
{
	return create_callsite(
			status, builder, scope, callsite, function,
			"temp_call_value", INTERNAL_LOC(), arguments);

	assert(!status);
	return nullptr;
}

bound_var_t::ref instantiate_unchecked_fn(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		unchecked_var_t::ref unchecked_fn,
		types::type::ref fn_type_unbound,
		unification_t unification)
{
	types::type::ref fn_type = fn_type_unbound->rebind(unification.bindings);

	debug_above(4, log(log_info, "it's time to instantiate %s with unified signature %s from %s",
				unchecked_fn->str().c_str(),
				fn_type->str().c_str(),
				unification.str().c_str()));

	/* save and later restore the current branch insertion point */
	llvm::IRBuilderBase::InsertPointGuard ipg(builder);

	ast::item::ref type_product = dyncast<const ast::type_product>(unchecked_fn->node);

	if (auto function_defn = dyncast<const ast::function_defn>(unchecked_fn->node)) {
		/* we shouldn't be here unless we found something to substitute */

		debug_above(4, log(log_info, "building substitution for %s with unification %s",
					function_defn->token.str().c_str(),
					unification.str().c_str()));

		/* create a generic substitution scope with the unification */
		scope_t::ref subst_scope = generic_substitution_scope_t::create(
				status, builder, unchecked_fn->node,
				unchecked_fn->module_scope, unification, fn_type);

		if (auto product = dyncast<const types::type_product>(fn_type)) {
			bound_type_t::refs args = create_bound_types_from_args(status,
					builder, subst_scope, product->dimensions[0]);

			bound_type_t::named_pairs named_args = zip_named_pairs(
					get_param_list_decl_variable_names(
						function_defn->decl->param_list_decl),
					args);

			bound_type_t::ref return_type = upsert_bound_type(status,
					builder, subst_scope, product->dimensions[1]);

			if (!!status) {
				/* instantiate the function we want */
				return function_defn->instantiate_with_args_and_return_type(status,
						builder, subst_scope, nullptr /*new_scope*/,
						named_args, return_type);
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

		auto data_ctor_sig = unchecked_data_ctor->sig->get_type(status);
		if (!!status) {
			debug_above(4, log(log_info, "going to bind ctor for %s",
						data_ctor_sig->str().c_str()));

			auto data_ctor_type = data_ctor_sig->rebind(unification.bindings);
			auto args_types = get_function_type_args(data_ctor_type);
			auto return_type = get_function_return_type(data_ctor_type);

			/* instantiate the data ctor we want */
			bound_var_t::ref ctor_fn = bind_ctor_to_scope(
					status, builder, subst_scope,
					unchecked_fn->id, node,
					args_types, return_type,
					unchecked_data_ctor->member_index);

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
		types::term::ref args)
{
	unification_t unification = fn->accepts_callsite(status, scope, args);
	if (!!status) {
		if (unification.result) {
			if (auto bound_fn = dyncast<const bound_var_t>(fn)) {
				/* this function has already been bound */
				debug_above(3, log(log_info, "override resolution has chosen %s",
							bound_fn->str().c_str()));
				return bound_fn;
			} else if (auto unchecked_fn = dyncast<const unchecked_var_t>(fn)) {
				/* we're instantiating a template or a forward decl */
				/* we know that fn and sig_args are compatible */
				types::term::ref fn_sig = fn->get_term();

				/* create the new callee signature type for building the generic
				 * substitution scope */
				auto env = scope->get_type_env();
				debug_above(5, log(log_info, "evaluating %s in %s with %s",
							fn_sig->str().c_str(),
							::str(env).c_str(),
							::str(unification.bindings).c_str()));
				types::type::ref fn_type_unbound = fn_sig->evaluate(env)->get_type(status);
				if (!!status) {
					return instantiate_unchecked_fn(status, builder, scope,
							unchecked_fn, fn_type_unbound, unification);
				}
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

	if (!!status) {
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
				user_error(status, *callsite, "no function found named " c_id("%s") " for callsite %s",
					   	alias.c_str(), callsite->str().c_str());
				debug_above(8, log(log_info, "%s", scope->str().c_str()));
			} else {
				std::stringstream ss;
				ss << string_format("unable to resolve overloads for " c_id("%s"),
						callsite->str().c_str());

				ss << "\n\targuments are " << args->str();

				for (auto &fn : fns) {
					ss << "\n\ttried " << fn->str();
				}
				user_error(status, *callsite, "%s", ss.str().c_str());
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
		user_error(status, callsite->get_location(), "failed to resolve function with args: %s",
				::str(var_args).c_str());

		assert(!status);
		return nullptr;
	}
}

