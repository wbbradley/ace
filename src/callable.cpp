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
#include "code_id.h"

#define USER_MAIN_FN "user/main"

bound_var_t::ref make_call_value(
		llvm::IRBuilder<> &builder,
		location_t location,
		scope_t::ref scope,
		life_t::ref life,
		bound_var_t::ref function,
		bound_var_t::refs arguments)
{
	return create_callsite(
			builder, scope, life, function,
			"temp_call_value", INTERNAL_LOC(), arguments);
}

	
bound_var_t::ref instantiate_data_type_ctor(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		unchecked_var_t::ref unchecked_fn,
		ast::item_t::ref node,
		types::type_function_t::ref fn_type,
		const unification_t *unification)
{
	/* we shouldn't be here unless we found something to substitute */
	debug_above(4, log(log_info, "building substitution for %s with %s",
				unchecked_fn->str().c_str(), node->token.str().c_str()));
	auto unchecked_data_ctor = dyncast<const unchecked_data_ctor_t>(unchecked_fn);
	assert(unchecked_data_ctor != nullptr);
	assert(!unchecked_data_ctor->native);

	scope_t::ref subst_scope;
	types::type_function_t::ref data_ctor_type;

	if (unification != nullptr) {
		/* create a generic substitution scope with the unification */
		subst_scope = generic_substitution_scope_t::create(
				builder, unchecked_fn->node,
				unchecked_fn->module_scope, *unification, fn_type);

		data_ctor_type = dyncast<const types::type_function_t>(
				unchecked_data_ctor->sig->rebind(unification->bindings));
	} else {
		subst_scope = unchecked_fn->module_scope;
		data_ctor_type = dyncast<const types::type_function_t>(unchecked_data_ctor->sig->rebind(scope->get_type_variable_bindings()));
	}

	assert(data_ctor_type != nullptr);
	debug_above(4, log(log_info, "going to bind ctor for %s", data_ctor_type->str().c_str()));

	/* instantiate the data ctor we want */
	bound_var_t::ref ctor_fn = bind_ctor_to_scope(
			builder, subst_scope,
			unchecked_fn->id, node->get_location(),
			data_ctor_type);

	/* the ctor should now exist */
	assert(ctor_fn != nullptr);
	return ctor_fn;
}

bound_var_t::ref instantiate_unchecked_fn(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		unchecked_var_t::ref unchecked_fn,
		types::type_function_t::ref fn_type,
		const unification_t *unification)
{
	static int depth = 0;
	depth_guard_t depth_guard(fn_type->get_location(), depth, 10);
	debug_above(5, log(log_info, "we are in scope " c_id("%s"), scope->get_name().c_str()));
	debug_above(5, log(log_info, "instantiating unchecked function %s : %s",
				unchecked_fn->str().c_str(),
				fn_type->str().c_str()));

	/* save and later restore the current branch insertion point */
	llvm::IRBuilderBase::InsertPointGuard ipg(builder);

	/* lifetimes have extents at function boundaries */
	auto life = make_ptr<life_t>(lf_function);

	if (auto function_defn = dyncast<const ast::function_defn_t>(unchecked_fn->node)) {
		/* we shouldn't be here unless we found something to substitute */
		scope_t::ref subst_scope;
		if (unification != nullptr) {
			debug_above(4, log(log_info, "building substitution for %s with unification %s",
						function_defn->token.str().c_str(),
						unification->str().c_str()));

			/* create a generic substitution scope with the unification */
			subst_scope = generic_substitution_scope_t::create(
					builder, unchecked_fn->node,
					unchecked_fn->module_scope, *unification, fn_type);
		} else {
			subst_scope = unchecked_fn->module_scope;
		}

		if (auto function = dyncast<const types::type_function_t>(fn_type)) {
			if (auto args = dyncast<const types::type_args_t>(function->args)) {
				types::type_t::ref type_constraints = (
						function->type_constraints
						? function->type_constraints->rebind(subst_scope->get_type_variable_bindings())
						: nullptr);

				try {
					bound_type_t::refs bound_args = upsert_bound_types(
							builder, subst_scope, args->args);

					std::vector<std::string> names;
					for (auto id : args->names) {
						names.push_back(id->get_name());
					}

					bound_type_t::named_pairs named_args = zip_named_pairs(names, bound_args);

					bound_type_t::ref return_type = upsert_bound_type(
							builder, subst_scope, function->return_type);

					/* instantiate the function we want */
					return instantiate_function_with_args_and_return_type(
							builder, subst_scope, life,
							unchecked_fn->id->get_token(), false /*as_closure*/,
							function_defn->decl->extends_module, nullptr /*new_scope*/,
							type_constraints, named_args, return_type, fn_type,
							function_defn->block);
				} catch (user_error &e) {
					std::throw_with_nested(user_error(
								unchecked_fn->get_location(),
								"while instantiating function %s",
								unchecked_fn->str().c_str()));
				}
			} else {
				panic("the arguments are not actually type_args_t");
			}
		} else {
			panic("we should have a product type for our fn_type");
		}
	} else if (ast::type_product_t::ref type_product = dyncast<const ast::type_product_t>(unchecked_fn->node)) {
		return instantiate_data_type_ctor(builder, scope, unchecked_fn, type_product, fn_type, unification);
	} else if (ast::data_type_t::ref data_type = dyncast<const ast::data_type_t>(unchecked_fn->node)) {
		return instantiate_data_type_ctor(builder, scope, unchecked_fn, data_type, fn_type, unification);
	} else {
		panic("we should only have function defn's in unchecked var's, right?");
		return nullptr;
	}

	return nullptr;
}

bound_var_t::ref check_func_vs_callsite(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		var_t::ref fn,
		types::type_t::ref args,
		types::type_t::ref return_type,
		int &coercions)
{
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

			types::type_function_t::ref fn_type = dyncast<const types::type_function_t>(fn->get_type(scope)->rebind(unification.bindings)->eval(scope));
			assert(fn_type != nullptr);

            if (auto bound_fn = scope->get_bound_function(fn->get_name(), fn_type->repr())) {
                /* function fn_type exists with name and signature we want, just use that */
                return bound_fn;
            }
			return instantiate_unchecked_fn(builder, scope, unchecked_fn, fn_type, &unification);
		} else {
			panic("unhandled var type");
			return nullptr;
		}
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
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		std::string alias,
		location_t location,
		types::type_t::ref args,
		types::type_t::ref return_type,
		var_t::refs &fns,
		fittings_t &fittings,
		bool check_unchecked,
		bool allow_coercions)
{
	debug_above(3, log(log_info, "maybe_get_callable(..., scope=%s, alias=%s, args=%s, ..., check_unchecked=%s, allow_coercions=%s)",
				scope->get_name().c_str(),
				alias.c_str(),
				args->str().c_str(),
				boolstr(check_unchecked),
				boolstr(allow_coercions)));

	llvm::IRBuilderBase::InsertPointGuard ipg(builder);

	/* look through the current scope stack and get a callable that is able
	 * to be invoked with the given args */
	fns.resize(0);
	scope->get_callables(alias, fns, check_unchecked);
	debug_above(7, log("looking for a " c_id("%s") " going to check:", alias.c_str()));
	for (auto fn : fns) {
		debug_above(7, log("callable %s : %s", fn->str().c_str(),
					fn->get_type(nullptr)->str().c_str()));
	}

	return get_best_fit(builder,
			scope->get_program_scope(),
			location,
			alias,
			args,
			return_type,
			fns,
			fittings,
			allow_coercions);
}

bound_var_t::ref get_callable_from_local_var(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		std::string alias,
		bound_var_t::ref bound_var,
		location_t callsite_location,
		types::type_t::ref args,
		types::type_t::ref return_type)
{
	/* make sure the function is just a function, not a reference to a function */
	auto resolved_bound_var = bound_var->resolve_bound_value(builder, scope);

	int coercions = 0;
	bound_var_t::ref callable = check_func_vs_callsite(builder,
			scope, callsite_location, resolved_bound_var, args, return_type, coercions);
	if (callable != nullptr) {
		return callable;
	} else {
		auto error = user_error(callsite_location, "variable " c_id("%s") " is not callable with these arguments or just isn't a function",
				alias.c_str());
		error.add_info(callsite_location, "type of %s is %s",
				alias.c_str(),
				bound_var->type->str().c_str());
		throw error;
	}

	return nullptr;
}

bound_var_t::ref get_callable(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		std::string alias,
		location_t callsite_location,
		types::type_args_t::ref args,
		types::type_t::ref return_type)
{
	auto runnable_scope = dyncast<runnable_scope_t>(scope);
	if (runnable_scope != nullptr) {
		/* if we're in a function, let's look for locally defined symbols */
		bound_var_t::ref bound_var = runnable_scope->get_bound_variable(
				builder, callsite_location, alias, runnable_scope->get_module_scope());
		if (bound_var != nullptr) {
			return get_callable_from_local_var(builder, runnable_scope, alias, bound_var, callsite_location,
					args, return_type);
		}
	}

	var_t::refs fns;
	fittings_t fittings;
	auto callable = maybe_get_callable(builder, scope, alias,
			callsite_location, args, return_type, fns, fittings);

	if (callable != nullptr) {
		return callable;
	} else {
		std::stringstream ss;
		if (fns.size() == 0) {
			ss << C_ID << alias << C_RESET;
			args->emit(ss, {}, 0 /*parent_precedence*/);
			ss << " not found";
		} else {
			ss << "unable to resolve overloads for " << C_ID << alias << C_RESET << args->str();
		}
		auto error = user_error(callsite_location, "%s", ss.str().c_str());

		/* report on the places we tried to look for a match */
		if (fns.size() > 10) {
			error.add_info(callsite_location,
					"%d non-matching functions called " c_id("%s")
					" found (skipping listing them all)", fittings.size(), alias.c_str());
		} else {
			for (auto &fn : fns) {
				ss.str("");
				ss << fn->get_type(scope)->str() << " did not match";
				error.add_info(fn->get_location(), "%s", ss.str().c_str());
			}
		}
		throw error;
	}
}

bound_var_t::ref call_program_function(
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

	try {
		/* get or instantiate a function we can call on these arguments */
		bound_var_t::ref function = get_callable(
				builder, program_scope, function_name, callsite_location,
				args, return_type);

		return make_call_value(builder, callsite_location, scope,
				life, function, var_args);
	} catch (user_error &e) {
		std::throw_with_nested(user_error(callsite_location, "failed to resolve function " c_id("%s") " with args: %s",
					function_name.c_str(),
					::str(var_args).c_str()));
	}
	return nullptr;
}

bound_var_t::ref call_module_function(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        std::string function_name,
		location_t callsite_location,
        const bound_var_t::refs var_args,
		types::type_t::ref return_type)
{
	types::type_args_t::ref args = get_args_type(var_args);
	auto module_scope = scope->get_module_scope();

	if (return_type == nullptr) {
		return_type = type_variable(callsite_location);
	}

	try {
		/* get or instantiate a function we can call on these arguments */
		bound_var_t::ref function = get_callable(
				builder, module_scope, function_name, callsite_location,
				args, return_type);

		return make_call_value(builder, callsite_location, scope,
				life, function, var_args);
	} catch (user_error &e) {
		std::throw_with_nested(user_error(callsite_location, "failed to resolve function " c_id("%s") " with args: %s",
					function_name.c_str(),
					::str(var_args).c_str()));
	}
	return nullptr;
}

std::string switch_std_main(std::string name) {
    if (!getenv("NO_STD_LIB")) {
        if (name == "main") {
            return USER_MAIN_FN;
        } else if (name == "__main__") {
            return "main";
        }
    }
    return name;
}

function_scope_t::ref make_function_scope(
		llvm::IRBuilder<> &builder,
		token_t token,
		scope_t::ref &scope,
		life_t::ref life,
		bool as_closure,
		bound_var_t::ref function_var,
		bound_type_t::named_pairs params)
{
	assert(life->life_form == lf_function);

	debug_above(5, log("creating function scope for " c_id("%s") " in " C_MODULE "%s" C_RESET,
			function_var->name.c_str(),
			scope->get_name().c_str()));
	auto new_scope = scope->new_function_scope(
			string_format("function-%s", function_var->name.c_str()));

	llvm::Function *llvm_function = llvm::cast<llvm::Function>(function_var->get_llvm_value());
	llvm::Function::arg_iterator args = llvm_function->arg_begin();

	assert(llvm_function->arg_size() == dyncast<const types::type_args_t>(dyncast<const types::type_function_t>(function_var->type->get_type())->args)->args.size());
	assert(llvm_function->arg_size() == params.size());

	int i = 0;

	for (auto &param : params) {
		llvm::Value *llvm_param = &(*args++);
		if (llvm_param->getName().str().size() == 0) {
			llvm_param->setName(param.first);
		}

		assert(!param.second->is_ref(scope));

		bool allow_reassignment = false;
		auto param_type = param.second->get_type();
		if (!param_type->eval_predicate(tb_ref, scope) && !param_type->eval_predicate(tb_null, scope)
				&& ((i != (int)params.size() - 1) || !as_closure)) {
			allow_reassignment = true;
		}

		/* create a slot for the final param value to be determined */
		llvm::Value *llvm_param_final = llvm_param;

		if (allow_reassignment) {
			param_type = type_ref(param_type);
			/* create an alloca in order to be able to reassign the named
			 * parameter to a new value. this does not mean that the parameter
			 * is an out param, we are simply enabling reuse of the name */
			llvm::AllocaInst *llvm_alloca = llvm_create_entry_block_alloca(
					llvm_function, param.second, param.first);

			// REVIEW: how to manage memory for named parameters? if we allow
			// changing their value then we have to enforce addref/release
			// semantics on them...
			debug_above(6, log(log_info, "creating a local alloca for parameter %s := %s",
						llvm_print(llvm_alloca).c_str(),
						llvm_print(llvm_param).c_str()));
			builder.CreateStore(llvm_param, llvm_alloca);	
			llvm_param_final = llvm_alloca;
		}

		auto bound_stack_var_type = upsert_bound_type(builder,
				scope, param_type);
		auto param_var = bound_var_t::create(INTERNAL_LOC(), param.first, bound_stack_var_type,
				llvm_param_final, make_iid(params[i++].first));


		// REVIEW: why is this here?
		// bound_type_t::ref return_type = get_function_return_type(builder, scope, function_var->type);

		life->track_var(builder, scope, param_var, lf_function);
		if (as_closure && i == (int)params.size()) {
			auto closure_scope = dyncast<closure_scope_t>(scope); // ->get_closure_scope();
			assert(closure_scope != nullptr);
			assert(!allow_reassignment);
			closure_scope->set_capture_env(param_var);
		} else {
			/* add the parameter argument to the current scope */
			new_scope->put_bound_variable(param.first, param_var);
		}
	}

	return new_scope;
}

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
		ast::block_t::ref block)
{
	program_scope_t::ref program_scope = scope->get_program_scope();
	std::string function_name = switch_std_main(name_token.text);

	dbg_when(name_token.text == "Just")
	indent_logger indent(name_token.location, 5,
			string_format("instantiating function " c_id("%s") " at %s", function_name.c_str(),
				name_token.location.str().c_str()));
	// debug_above(9, log("function has env %s", ::str(scope->get_total_env()).c_str()));
	debug_above(9, log("function has bindings %s", ::str(scope->get_type_variable_bindings()).c_str()));

	/* let's make sure we're not instantiating a function we've already instantiated */
	assert(!scope->get_bound_function(function_name, fn_type->repr()));

	assert(life->life_form == lf_function);
	assert(life->values.size() == 0);

	assert(scope->get_llvm_module() != nullptr);

	auto function_type = get_function_type(type_constraints, args, return_type)->eval(scope);
	bound_type_t::ref bound_function_type = upsert_bound_type(builder, scope, function_type);
	assert(bound_function_type->get_type()->get_signature() == fn_type->eval(scope)->repr());

	bound_var_t::ref already_bound_function;
	if (scope->has_bound(function_name, bound_function_type->get_type(), &already_bound_function)) {
		return already_bound_function;
	}

	llvm::IRBuilderBase::InsertPointGuard ipg(builder);

	assert(bound_function_type->get_llvm_type() != nullptr);

	llvm::Type *llvm_type = bound_function_type->get_llvm_specific_type();
	if (llvm_type->isPointerTy()) {
		llvm_type = llvm_type->getPointerElementType();
	}
	debug_above(5, log(log_info, "creating function %s with LLVM type %s",
				function_name.c_str(),
				llvm_print(llvm_type).c_str()));
	assert(llvm_type->isFunctionTy());

	/* Create a user-defined function */
	llvm::Function *llvm_function = llvm::Function::Create(
			(llvm::FunctionType *)llvm_type,
			llvm::Function::ExternalLinkage, function_name,
			scope->get_llvm_module());

	// TODO: enable inlining for various functions
	// llvm_function->addFnAttr(llvm::Attribute::AlwaysInline);
	llvm_function->setGC(GC_STRATEGY);
	llvm_function->setDoesNotThrow();

	/* start emitting code into the new function. caller should have an
	 * insert point guard */
	llvm::BasicBlock *llvm_entry_block = llvm::BasicBlock::Create(builder.getContext(),
			"entry", llvm_function);
	llvm::BasicBlock *llvm_body_block = llvm::BasicBlock::Create(builder.getContext(),
			"body", llvm_function);

	builder.SetInsertPoint(llvm_entry_block);
	/* leave an empty entry block so that we can insert GC stuff in there, but be able to
	 * seek to the end of it and not get into business logic */
	builder.CreateBr(llvm_body_block);

	builder.SetInsertPoint(llvm_body_block);

	if (getenv("TRACE_FNS") != nullptr) {
		std::stringstream ss;
		ss << name_token.location.str() << ": " << (name_token.text.size() != 0 ? name_token.text + " : " : "") << bound_function_type->str();
		auto callsite_debug_function_name_print = expand_callsite_string_literal(
				name_token,
				"posix",
				"puts",
				ss.str());
		callsite_debug_function_name_print->resolve_statement(builder, scope, life, nullptr, nullptr);
	}

	/* set up the mapping to this function for use in recursion */
	bound_var_t::ref function_var = bound_var_t::create(
			INTERNAL_LOC(), name_token.text, bound_function_type, llvm_function,
		   	make_code_id(name_token));

	/* we should be able to check its block as a callsite. note that this
	 * code will also run for generics but only after the
	 * sbk_generic_substitution mechanism has run its course. */
	auto function_scope = make_function_scope(builder, name_token, scope,
			life, as_closure, function_var, args);

		/* now put this function declaration into the containing scope in case
		 * of recursion */
	if (function_var->name.size() != 0) {
		debug_above(7, log("%s should be %s",
					function_var->type->get_type()->repr().c_str(),
					fn_type->eval(scope)->repr().c_str()));

		assert(function_var->get_signature() == fn_type->eval(scope)->repr());
		put_bound_function(
				builder, scope, name_token.location,
			   	function_var->name, extends_module, function_var,
				new_scope);
	}

	bool all_paths_return = false;
	debug_above(7, log("deeper %s", function_scope->get_name().c_str()));

	try {
		/* keep track of whether this function returns */
		debug_above(7, log("setting return_type_constraint in %s to %s %s",
				   	function_var->name.c_str(),
					return_type->str().c_str(),
					llvm_print(return_type->get_llvm_type()).c_str()));
		function_scope->set_return_type_constraint(return_type);

		block->resolve_statement(builder, function_scope, life,
				nullptr, &all_paths_return);

	} catch (user_error &e) {
		std::throw_with_nested(user_error(name_token.location,
					"while checking %s", function_var->str().c_str()));
	}

	debug_above(10, log(log_info, "module dump from %s\n%s",
				__PRETTY_FUNCTION__,
				llvm_print_module(*llvm_get_module(builder)).c_str()));

	if (all_paths_return) {
		llvm_verify_function(name_token.location, llvm_function);
		return function_var;
	} else {
		/* not all control paths return */
		if (return_type->is_void(scope)) {
			/* if this is a void let's give the user a break and insert
			 * a default void return */

			life->release_vars(builder, scope, lf_function);
			builder.CreateRetVoid();
			llvm_verify_function(name_token.location, llvm_function);
			return function_var;
		} else {
			/* no breaks here, we don't know what to return */
			throw user_error(name_token.location, "not all control paths return a value");
		}
	}
}

