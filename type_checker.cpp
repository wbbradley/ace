#include "zion.h"
#include "logger.h"
#include "type_checker.h"
#include "utils.h"
#include "callable.h"
#include "compiler.h"
#include "llvm_zion.h"
#include "llvm_utils.h"
#include "json.h"
#include "ast.h"
#include "llvm_types.h"
#include "parser.h"
#include "unification.h"
#include "code_id.h"
#include "patterns.h"
#include <iostream>

/*
 * The basic idea here is that type checking is a graph operation which can be
 * ordered topologically based on dependencies between callers and callees.
 * Luckily our AST has exactly that structure.  We will perform a topological
 * sort by resolving types as we return from our depth first traversal.
 */


/************************************************************************/

bound_type_t::ref get_fully_bound_param_info(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ast::var_decl &obj,
		scope_t::ref scope,
		atom &var_name,
		atom::set &generics,
		int &generic_index)
{
	/* get the name of this parameter */
	var_name = obj.token.text;

	assert(obj.type != nullptr);

	auto type_id_name = make_type_id_code_id(
			obj.token.location,
			var_name);

	/* the user specified a type */
	if (!!status) {
		debug_above(6, log(log_info, "upserting type for param %s at %s",
					obj.type->str().c_str(),
					obj.type->get_location().str().c_str()));
		return upsert_bound_type(status, builder, scope, obj.type);
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref type_check_bound_var_decl(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ast::var_decl &obj,
		scope_t::ref scope,
		bool maybe_unbox)
{
	const atom symbol = obj.token.text;

	debug_above(4, log(log_info, "type_check_var_decl is looking for a type for variable " c_var("%s") " : %s",
				symbol.c_str(), obj.str().c_str()));

	if (scope->has_bound_variable(symbol, rc_capture_level)) {
		// TODO: get a pointer to the prior var
		user_error(status, obj, "variables cannot be redeclared");
		debug_above(2, log(log_info, "%s", scope->str().c_str()));
		return nullptr;
	}

	/* 'init_var' is keeping track of the value we are assigning to our new
	 * variable (if any exists.) */
	bound_var_t::ref init_var;

	/* 'type' is keeping track of what the variable's ending type will be */
	bound_type_t::ref bound_type;

	/* 'declared_type' tells us the user-declared type on the left-hand side of
	 * the assignment. this is generally used to allow a variable to be more
	 * generalized than the specific right-hand side initial value might be. */
	types::type::ref declared_type;

	/* 'unboxed' tracks whether we are doing maybe unboxing for this var_decl */
	bool unboxed = false;

	/* 'condition_value' refers to whether this was an unboxed maybe */
	bound_var_t::ref condition_value;

	assert(obj.type != nullptr);

	if (obj.initializer) {
		/* we have an initializer */
		init_var = obj.initializer->resolve_instantiation(status, builder,
				scope, nullptr, nullptr);
	}

	if (!!status) {
		/* we have a declared type on the left-hand side */
		declared_type = obj.type->rebind(scope->get_type_variable_bindings());

		types::type::ref lhs_type = declared_type;
		if (init_var != nullptr) {
			/* we have an initializer */
			if (declared_type != nullptr) {
				/* ensure 'init_var' <: 'declared_type' */
				unification_t unification = unify(
						declared_type,
						init_var->get_type(),
						scope->get_typename_env());

				if (unification.result) {
					/* the lhs is a supertype of the rhs */
					lhs_type = declared_type->rebind(unification.bindings);
				} else {
					/* report that the variable type does not match the initializer type */
					user_error(status, obj, "declared type of `" c_var("%s") "` does not match type of initializer",
							obj.token.text.c_str());
					user_message(log_info, status, init_var->get_location(), c_type("%s") " != " c_type("%s"),
							declared_type->str().c_str(),
							init_var->type->str().c_str());
				}
			} else {
				/* we must get the type from the initializer */
				lhs_type = init_var->type->get_type();
			}
		}

		if (!!status) {
			/* resolve the type of the variable being declared */
			assert(lhs_type != nullptr);

			if (maybe_unbox) {
				debug_above(3, log(log_info, "attempting to unbox %s", obj.str().c_str()));

				/* try to see if we can unbox this if it's a Maybe */
				if (init_var == nullptr) {
					user_error(status, obj.get_location(), "missing initialization value");
				} else {
					/* since we are maybe unboxing, then let's first off see if
					 * this is even a maybe type. */
					if (auto maybe_type = dyncast<const types::type_maybe>(lhs_type)) {
						/* looks like the initialization variable is a supertype
						 * of the nil type */
						unboxed = true;

						bound_type = upsert_bound_type(status, builder, scope,
								maybe_type->just);
					} else {
						/* this is not a maybe, so let's just move along */
					}
				}
			}

			if (bound_type == nullptr) {
				bound_type = upsert_bound_type(status, builder, scope, lhs_type);
			}
		}

		if (!!status) {
			assert(bound_type != nullptr);

			/* generate the mutable stack-based variable for this var */
			llvm::Function *llvm_function = llvm_get_function(builder);
			llvm::AllocaInst *llvm_alloca = llvm_create_entry_block_alloca(llvm_function,
					bound_type, symbol);

			if (init_var) {
				debug_above(6, log(log_info, "creating a store instruction %s := %s",
							llvm_print_value_ptr(llvm_alloca).c_str(),
							llvm_print_value_ptr(init_var->llvm_value).c_str()));
				builder.CreateStore(llvm_resolve_alloca(builder, init_var->llvm_value), llvm_alloca);	
			} else {
				if (dyncast<const types::type_maybe>(lhs_type)) {
					/* this can be null, let's initialize it as such */
					llvm::Constant *llvm_null_value = llvm::Constant::getNullValue(bound_type->get_llvm_type());
					builder.CreateStore(llvm_null_value, llvm_alloca);
				} else {
					user_error(status, obj, "missing initializer");
				}
			}

			if (!!status) {
				/* the reference_expr that looks at this llvm_value will need to
				 * know to use store/load semantics, not just pass-by-value */
				bound_var_t::ref var_decl_variable = bound_var_t::create(INTERNAL_LOC(), symbol,
						bound_type, llvm_alloca, make_code_id(obj.token),
						true /*is_lhs*/);

				/* on our way out, stash the variable in the current scope */
				scope->put_bound_variable(status, var_decl_variable->name,
						var_decl_variable);

				if (unboxed) {
					assert(init_var != nullptr);

					/* get the maybe type so that we can use it as a conditional */
					bound_type_t::ref condition_type = upsert_bound_type(status, builder, scope, lhs_type);
					condition_value = bound_var_t::create(INTERNAL_LOC(), symbol,
							condition_type, init_var->llvm_value, make_code_id(obj.token),
							false /*is_lhs*/);
				}

				if (!!status) {
					if (condition_value != nullptr) {
						/* we're unboxing a Maybe{any}, so let's return
						 * whether this was Empty or not... */
						assert(unboxed);
						assert(maybe_unbox);
						return condition_value;
					} else {
						return var_decl_variable;
					}
				}
			}
		}
	}

	assert(!status);
	return nullptr;
}

atom::many get_param_list_decl_variable_names(ast::param_list_decl::ref obj) {
	atom::many names;
	for (auto param : obj->params) {
		names.push_back({param->token.text});
	}
	return names;
}

bound_type_t::named_pairs zip_named_pairs(
		atom::many names,
		bound_type_t::refs args)
{
	bound_type_t::named_pairs named_args;
	assert(names.size() == args.size());
	for (int i = 0; i < args.size(); ++i) {
		named_args.push_back({names[i], args[i]});
	}
	return named_args;
}

status_t get_fully_bound_param_list_decl_variables(
		llvm::IRBuilder<> &builder,
		ast::param_list_decl &obj,
		scope_t::ref scope,
		bound_type_t::named_pairs &params)
{
	status_t status;

	/* we keep track of the generic parameters to ensure equivalence */
	atom::set generics;
	int generic_index = 1;

	for (auto param : obj.params) {
		atom var_name;
		bound_type_t::ref param_type = get_fully_bound_param_info(status,
				builder, *param, scope, var_name, generics, generic_index);

		if (!!status) {
			params.push_back({var_name, param_type});
		}
	}
	return status;
}

bound_type_t::ref get_return_type_from_return_type_expr(
		status_t &status,
		llvm::IRBuilder<> &builder,
		types::type::ref type,
		scope_t::ref scope)
{
	/* lookup the alias, default to void */
	if (type != nullptr) {
		return upsert_bound_type(status, builder, scope, type);
	} else {
		/* user specified no return type, default to void */
		return scope->get_program_scope()->get_bound_type({"void"});
	}

	assert(!status);
	return nullptr;
}

void type_check_fully_bound_function_decl(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ast::function_decl &obj,
		scope_t::ref scope,
		types::type::ref &inbound_context,
		bound_type_t::named_pairs &params,
		bound_type_t::ref &return_value)
{
	/* returns the parameters and the return value types fully resolved */
	debug_above(4, log(log_info, "type checking function decl %s", obj.token.str().c_str()));

	if (obj.inbound_context != nullptr) {
		inbound_context = obj.inbound_context;

		if (!status) {
			user_message(log_info, status, obj, "while instantiating %s", obj.token.str().c_str());
			return;
		}
	} else {
		/* this function does not have a context declaration, use the current
		 * module's type (which basically defaults to private scope */
		inbound_context = scope->get_inbound_context();
	}

	if (obj.param_list_decl) {
		/* the parameter types as per the decl */
		status |= get_fully_bound_param_list_decl_variables(builder,
				*obj.param_list_decl, scope, params);

		if (!!status) {
			return_value = get_return_type_from_return_type_expr(status,
					builder, obj.return_type, scope);

			/* we got the params, and the return value */
			return;
		}
	} else {
		user_error(status, obj, "no param_list_decl was present");
	}

	assert(!status);
}

bool type_is_unbound(types::type::ref type, types::type::map bindings) {
	return type->rebind(bindings)->ftv_count() > 0;
}

bool is_function_defn_generic(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		const ast::function_defn &obj)
{
	if (obj.decl->param_list_decl) {
		/* check the parameters' genericity */
		auto &params = obj.decl->param_list_decl->params;
		for (auto &param : params) {
			if (!param->type) {
				debug_above(3, log(log_info, "found a missing parameter type on %s, defaulting it to an unnamed generic",
							param->str().c_str()));
				return true;
			}

			if (!!status) {
				if (type_is_unbound(param->type, scope->get_type_variable_bindings())) {
					debug_above(3, log(log_info, "found a generic parameter type on %s",
								param->str().c_str()));
					return true;
				}
			} else {
				/* failed to check type genericity */
				panic("what now hey?");
				return true;
			}
		}
	} else {
		panic("function declaration has no parameter list");
	}

	if (!!status) {
		if (obj.decl->return_type != nullptr) {
			/* check the return type's genericity */
			return obj.decl->return_type->ftv_count() > 0;
		} else {
			/* default to void, which is fully bound */
			return false;
		}
	}

	assert(!status);
	return false;
}

function_scope_t::ref make_param_list_scope(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ast::function_decl &obj,
		scope_t::ref &scope,
		bound_var_t::ref function_var,
		bound_type_t::named_pairs params)
{
	/* this function is coupled to the sbk_generic_substitution mechanism.
	 * when we're not dealing with generics, it simply looks up the types in
	 * the decl parameter's type expressions. when we're dealing with generics
	 * after using the incoming scope to find the bound parameter types (based
	 * on upstream callsite arguments), we drop the sbk_generic_substitution in
	 * order to prevent those type names from being visible within the function.
	 */
	assert(!!status);

	if (!!status) {
		auto new_scope = scope->new_function_scope(
				string_format("function-%s", function_var->name.c_str()));

		assert(obj.param_list_decl->params.size() == params.size());

		llvm::Function *llvm_function = llvm::cast<llvm::Function>(function_var->llvm_value);
		llvm::Function::arg_iterator args = llvm_function->arg_begin();

		int i = 0;

		for (auto &param : params) {
			llvm::Value *llvm_param = args++;
			llvm_param->setName(param.first.str());

			/* create an alloca in order to be able to reassign the named
			 * parameter to a new value. this does not mean that the parameter
			 * is an out param, we are simply enabling reuse of the name */
			llvm::AllocaInst *llvm_alloca = llvm_create_entry_block_alloca(
					llvm_function, param.second, param.first.str());

			debug_above(6, log(log_info, "creating a local alloca for parameter %s := %s",
						llvm_print_value_ptr(llvm_alloca).c_str(),
						llvm_print_value_ptr(llvm_param).c_str()));
			builder.CreateStore(llvm_param, llvm_alloca);	

			/* add the parameter argument to the current scope */
			new_scope->put_bound_variable(status, param.first,
					bound_var_t::create(INTERNAL_LOC(), param.first, param.second,
						llvm_alloca, make_code_id(obj.param_list_decl->params[i++]->token),
						true/*is_lhs*/));
			if (!status) {
				break;
			}
		}

		if (!!status) {
			return new_scope;
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::link_module_statement::resolve_instantiation(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	module_scope_t::ref module_scope = dyncast<module_scope_t>(scope);
	assert(module_scope != nullptr);

	auto linked_module_name = extern_module->get_canonical_name();
	assert(linked_module_name.size() != 0);

	program_scope_t::ref program_scope = scope->get_program_scope();
	module_scope_t::ref linked_module_scope = program_scope->lookup_module(linked_module_name);

	if (linked_module_scope != nullptr) {
		/* put the module into program scope as a named variable. this is to
		 * enable dot-expressions to resolve module scope lookups. note that
		 * the module variables are not reified into the actual generated LLVM
		 * IR.  they are resolved entirely at compile time.  perhaps in a
		 * future version they can be used as run-time variables, so that we
		 * can pass modules around for another level of polymorphism. */
		bound_module_t::ref module_variable = bound_module_t::create(INTERNAL_LOC(),
				link_as_name.text, make_code_id(token), linked_module_scope);

		module_scope->put_bound_variable(status, module_variable->name, module_variable);

		if (!!status) {
			return module_variable;
		}
	} else {
		user_error(status, *this, "can't find module %s", linked_module_name.c_str());
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::link_function_statement::resolve_instantiation(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	/* FFI */
	module_scope_t::ref module_scope = dyncast<module_scope_t>(scope);
	assert(module_scope);

	if (!scope->has_bound_variable(function_name.text, rc_just_current_scope)) {
		types::type::ref inbound_context;
		bound_type_t::named_pairs named_args;
		bound_type_t::ref return_value;

		type_check_fully_bound_function_decl(status, builder, *extern_function,
				scope, inbound_context, named_args, return_value);

		if (!!status) {
			bound_type_t::refs args;
			for (auto &named_arg_pair : named_args) {
				args.push_back(named_arg_pair.second);
			}

			// TODO: rearrange this, and get the pointer type
			llvm::FunctionType *llvm_func_type = llvm_create_function_type(
					status, builder, args, return_value);

			/* try to find this function, if it already exists... */
			llvm::Module *llvm_module = module_scope->get_llvm_module();
			llvm::Value *llvm_value = llvm_module->getOrInsertFunction(function_name.text,
					llvm_func_type);

			assert(llvm_print_type(*llvm_value->getType()) != llvm_print_type(*llvm_func_type));

			/* get the full function type */
			types::type_function::ref function_sig = get_function_type(
					inbound_context, args, return_value);
			debug_above(3, log(log_info, "%s has type %s",
						function_name.str().c_str(),
						function_sig->str().c_str()));

			/* actually create or find the finalized bound type for this function */
			bound_type_t::ref bound_function_type = upsert_bound_type(
					status, builder, scope, function_sig);

			return bound_var_t::create(
					INTERNAL_LOC(),
					scope->make_fqn(function_name.text),
					bound_function_type,
					llvm_value,
					make_code_id(extern_function->token),
					false/*is_lhs*/);
		}
	} else {
		user_error(status, *this, "name conflict with %s", function_name.text.c_str());
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::dot_expr::resolve_overrides(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		const ptr<const ast::item> &callsite,
		const bound_type_t::refs &args) const
{
	indent_logger indent(5, string_format(
				"dot_expr::resolve_overrides for %s",
				callsite->str().c_str()));

	/* check the left-hand side first, it should be a type_namespace */
	bound_var_t::ref lhs_var = lhs->resolve_instantiation(
			status, builder, scope, nullptr, nullptr);

	if (!!status) {
		if (auto bound_module = dyncast<const bound_module_t>(lhs_var)) {
			assert(bound_module->module_scope != nullptr);

			/* let's see if the associated module has a method that can handle this callsite */
			return get_callable(status, builder, bound_module->module_scope, rhs.text, callsite,
					bound_module->module_scope->get_outbound_context(),
					get_args_type(args));
		} else {
			user_error(status, *lhs, "left of a dot (\".\") must be a struct or module. this is not a struct or module. %s",
					lhs_var->str().c_str());
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::callsite_expr::resolve_instantiation(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	/* get the value of calling a function */
	bound_type_t::refs param_types;
	bound_var_t::refs arguments;

	if (auto symbol = dyncast<ast::reference_expr>(function_expr)) {
		if (symbol->token.text == "static_print") {
			if (params->expressions.size() == 1) {
				auto param = params->expressions[0];
				bound_var_t::ref param_var = param->resolve_instantiation(
						status, builder, scope, nullptr, nullptr);

				if (!!status) {
					user_message(log_info, status, param->get_location(),
							"%s : %s", param->str().c_str(),
							param_var->type->str().c_str());
					return nullptr;
				}

				assert(!status);
				return nullptr;
			} else {
				user_error(status, *shared_from_this(),
						"static_print requires one and only one parameter");

				assert(!status);
				return nullptr;
			}
		}
	}

	if (params && params->expressions.size() != 0) {
		/* iterate through the parameters and add their types to a vector */
		for (auto &param : params->expressions) {
			bound_var_t::ref param_var = param->resolve_instantiation(
					status, builder, scope, nullptr, nullptr);

			if (!status) {
				break;
			}

			arguments.push_back(param_var);
			param_types.push_back(param_var->type);
		}
	} else {
		/* the callsite has no parameters */
	}

	if (!!status) {
		if (auto can_reference_overloads = dyncast<can_reference_overloads_t>(function_expr)) {
			/* we need to figure out which overload to call, if there are any */
			bound_var_t::ref function = can_reference_overloads->resolve_overrides(
					status, builder, scope, shared_from_this(),
					bound_type_t::refs_from_vars(arguments));

			if (!!status) {
				debug_above(5, log(log_info, "function chosen is %s", function->str().c_str()));

				return make_call_value(status, builder, shared_from_this(), scope,
						function, arguments);
			}
		} else {
			user_error(status, *function_expr,
					"%s being called like a function. arguments are %s",
					function_expr->str().c_str(),
					::str(arguments).c_str());
			return nullptr;
		}
	}


	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::reference_expr::resolve_instantiation(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	/* we wouldn't be referencing a variable name here unless it was unique
	 * override resolution only happens on callsites, and we don't allow
	 * passing around unresolved overload references */
	bound_var_t::ref var = scope->get_bound_variable(status, shared_from_this(), token.text);

	if (!var) {
		user_error(status, *this, "undefined symbol " c_id("%s"), token.text.c_str());
	}

	return var;
}

bound_var_t::ref ast::reference_expr::resolve_as_condition(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		local_scope_t::ref *new_scope) const
{
	/* we wouldn't be referencing a variable name here unless it was unique
	 * override resolution only happens on callsites, and we don't allow
	 * passing around unresolved overload references */
	bound_var_t::ref var = scope->get_bound_variable(status, shared_from_this(), token.text);

	if (!var) {
		user_error(status, *this, "undefined symbol " c_id("%s"), token.text.c_str());
	}

	if (auto maybe_type = dyncast<const types::type_maybe>(var->type->get_type())) {
		runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
		assert(runnable_scope);

		/* variable declarations begin new scopes */
		local_scope_t::ref fresh_scope = runnable_scope->new_local_scope(
				string_format("if-assignment-%s", token.text.c_str()));

		scope = fresh_scope;
		*new_scope = fresh_scope;

		/* looks like the initialization variable is a supertype
		 * of the nil type */
		auto bound_type = upsert_bound_type(status, builder, scope,
				maybe_type->just);

		if (!!status) {
			/* because we're evaluating this maybe value in the context of a
			 * condition (super simplified at this point), let's redeclare it
			 * without its maybe, since we know it will be valid if the
			 * condition passes */
			bound_var_t::ref var_decl_variable = bound_var_t::create(INTERNAL_LOC(), token.text,
					bound_type, var->llvm_value, make_code_id(token),
					var->is_lhs /*is_lhs*/);

			/* on our way out, stash the variable in the current scope */
			scope->put_bound_variable(status, var_decl_variable->name,
					var_decl_variable);

			/* get the maybe type so that we can use it as a conditional */
			bound_type_t::ref condition_type = upsert_bound_type(status, builder, scope, maybe_type);
			if (!!status) {
				bound_var_t::ref condition_value = bound_var_t::create(INTERNAL_LOC(), token.text,
						condition_type, var->llvm_value, make_code_id(token),
						false /*is_lhs*/);
				return condition_value;
			}
		}

		assert(!status);
		return nullptr;
	} else {
		/* this is not a maybe, so let's just move along */
	}

	return var;
}

bound_var_t::ref ast::array_index_expr::resolve_instantiation(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	/* this expression looks like this
	 *
	 *   lhs[index]
	 *
	 */

	if (!!status) {
		bound_var_t::ref lhs_val = lhs->resolve_instantiation(status, builder,
				scope, nullptr, nullptr);

		if (!!status) {
			/* check to see if we have a literal index */
			if (auto literal_expr = dyncast<ast::literal_expr>(index)) {
				/* check to see if it is an integer */
				if (literal_expr->token.tk == tk_integer) {
					int64_t value = atoll(literal_expr->token.text.c_str());

					/* see if we have a deref operator function for the lhs type */
					return call_const_subscript_operator(status, builder,
							scope, shared_from_this(), lhs_val,
							make_code_id(literal_expr->token), value);
				} else {
					user_error(status, *this,
							"tuple dereferencing with " c_internal("%s") " is not yet impl",
							tkstr(literal_expr->token.tk));
				}
			} else {
				user_error(status, *this, "not impl");
			}
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::array_literal_expr::resolve_instantiation(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	user_error(status, *this, "not impl");
	return nullptr;
}

bound_var_t::ref type_check_binary_operator(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		ptr<const ast::expression> lhs,
		ptr<const ast::expression> rhs,
		ast::item::ref obj,
		atom function_name)
{
	if (!!status) {
		assert(function_name.size() != 0);

		bound_var_t::ref lhs_var, rhs_var;
		lhs_var = lhs->resolve_instantiation(status, builder, scope, nullptr, nullptr);
		if (!!status) {
			rhs_var = rhs->resolve_instantiation(status, builder, scope, nullptr, nullptr);

			if (!!status) {
				/* get or instantiate a function we can call on these arguments */
				return call_program_function(
						status, builder, scope, function_name,
						obj, {lhs_var, rhs_var});
			}
		}
	}
	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::eq_expr::resolve_instantiation(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	atom function_name;
	switch (token.tk) {
	case tk_equal:
		function_name = "__eq__";
		break;
	case tk_inequal:
		function_name = "__ineq__";
		break;
	default:
		return null_impl();
	}

	return type_check_binary_operator(status, builder, scope, lhs, rhs,
			shared_from_this(), function_name);
}

bound_var_t::ref ast::tuple_expr::resolve_instantiation(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	/* let's get the actual values in our tuple. */
	bound_var_t::refs vars;
	vars.reserve(values.size());

	for (auto &value: values) {
		bound_var_t::ref var = value->resolve_instantiation(status, builder,
				scope, nullptr, nullptr);
		if (!!status) {
			vars.push_back(var);
		}
	}

	if (!!status) {
		bound_type_t::refs args = get_bound_types(vars);

		/* let's get the type for this tuple wrapped as an object */
		types::type::ref tuple_type = get_tuple_type(args);

		/* now, let's see if we already have a ctor for this tuple type, if not
		 * we'll need to create a data ctor for this unnamed tuple type */
		auto program_scope = scope->get_program_scope();

		std::pair<bound_var_t::ref, bound_type_t::ref> tuple = instantiate_tuple_ctor(
				status, builder, scope,
				scope->get_inbound_context(), args,
				make_iid(tuple_type->repr()), shared_from_this());

		if (!!status) {
			assert(get_function_return_type(status,
						builder, *shared_from_this(),
						scope, tuple.first->type)->get_type()->repr() == tuple_type->repr());

			/* now, let's call our unnamed tuple ctor and return that value */
			return create_callsite(status, builder, scope, shared_from_this(), tuple.first,
					tuple_type->repr(), token.location,
					vars);
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::or_expr::resolve_instantiation(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	// TODO: implement short-circuiting
	return null_impl();
}

bound_var_t::ref ast::and_expr::resolve_instantiation(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	// TODO: implement short-circuiting
	return null_impl();
}

bound_type_t::ref eval_to_bound_struct_ref(
		status_t &status,
		scope_t::ref scope,
		ast::item::ref node,
		bound_type_t::ref bound_type)
{
	if (bound_type->is_ref()) {
		return bound_type;
	} else if (bound_type->is_maybe()) {
		user_error(status, node->get_location(),
				"maybe type %s cannot be dereferenced. todo implement ?.",
				bound_type->str().c_str());
		return nullptr;
	}

	types::type::ref expansion = eval(bound_type->get_type(),
			scope->get_typename_env());

	if (expansion != nullptr) {
		debug_above(5, log(log_info,
					"expanded %s to %s",
					bound_type->str().c_str(),
					expansion->str().c_str()));
		auto bound_expansion = scope->get_bound_type(expansion->get_signature());
		assert(bound_expansion != nullptr);
		return bound_expansion;
	} else {
		user_error(status, node->get_location(),
				"maybe type %s cannot be dereferenced. todo implement ?.",
				bound_type->str().c_str());
	}

	assert(!status);
	return nullptr;
}

types::type_product::ref get_struct_type_from_ref(
		status_t &status,
	   	ast::item::ref node,
	   	bound_type_t::ref bound_struct_ref)
{
	if (auto product = dyncast<const types::type_product>(
				bound_struct_ref->get_type())) {
		if (product->pk == pk_ref) {
			auto struct_type = dyncast<const types::type_product>(product->dimensions[0]);
			if (struct_type != nullptr) {
				if (struct_type->pk == pk_tuple) {
					return struct_type;
				} else {
					user_error(status, node->get_location(),
							"%s is pointing to %s, which is not a struct",
							bound_struct_ref->str().c_str(),
							struct_type->str().c_str());
				}
			}
		} else {
			user_error(status, node->get_location(),
					"unable to dereference %s, it's not a ref",
					node->str().c_str());
		}
	}
	assert(!status);
	return nullptr;
}

bound_var_t::ref extract_member_variable(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		ast::item::ref node,
		bound_var_t::ref bound_var,
		atom member_name,
		bound_type_t::ref bound_type)
{
	bound_type_t::ref bound_struct_ref = eval_to_bound_struct_ref(status, scope,
			node, bound_type);

	if (!status) {
		return nullptr;
	}

	types::type_product::ref struct_type = get_struct_type_from_ref(
			status, node, bound_struct_ref);

	if (!status) {
		return nullptr;
	}

	auto member_index = struct_type->name_index;
	auto member_index_iter = member_index.find(member_name);

	for (auto member_index_pair : member_index) {
		debug_above(5, log(log_info, "%s: %d", member_index_pair.first.c_str(),
					member_index_pair.second));
	}

	if (member_index_iter != member_index.end()) {
		auto index = member_index_iter->second;
		debug_above(5, log(log_info, "found member %s of type %s at index %d",
					member_name.c_str(),
					bound_struct_ref->str().c_str(), index));

		/* get the type of the dimension being referenced */
		bound_type_t::ref member_type = scope->get_bound_type(
				struct_type->dimensions[index]->get_signature());
		assert(bound_struct_ref->get_llvm_type() != nullptr);

		llvm::Value *llvm_var_value = llvm_resolve_alloca(builder, bound_var->llvm_value);
		if (!bound_struct_ref->get_llvm_type()->isPointerTy()) {
			user_error(status, node->get_location(), "type is not a pointer type: %s",
					bound_struct_ref->str().c_str());
		}
		if (!!status) {
			llvm::Value *llvm_value_as_specific_type = builder.CreatePointerBitCastOrAddrSpaceCast(
					llvm_var_value, bound_struct_ref->get_llvm_type());

			llvm::Value *llvm_gep = builder.CreateInBoundsGEP(
					llvm_value_as_specific_type,
					{builder.getInt32(0), builder.getInt32(index)});

			llvm::Value *llvm_item = builder.CreateLoad(llvm_gep);
			return bound_var_t::create(
					INTERNAL_LOC(), string_format(".%s", member_name.c_str()),
					member_type, llvm_item, make_iid(member_name), false/*is_lhs*/);
		}
	} else {
		auto bindings = scope->get_type_variable_bindings();
		auto full_type = bound_var->type->get_type()->rebind(bindings);
		user_error(status, node->get_location(),
			   	"%s has no dimension called " c_id("%s"),
				full_type->str().c_str(),
				member_name.c_str());
		user_message(log_info, status, bound_var->type->get_location(), "%s has dimension(s) [%s]",
				full_type->str().c_str(),
				join_with(member_index, ", ", [] (std::pair<atom, int> index) -> std::string {
					return std::string(C_ID) + index.first.str() + C_RESET;
					}).c_str());
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::dot_expr::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	bound_var_t::ref lhs_val = lhs->resolve_instantiation(status,
			builder, scope, nullptr, nullptr);

	if (!!status) {
		auto bound_type = lhs_val->type;
		types::type::ref member_type;

		if (!!status) {
			return extract_member_variable(status, builder, scope,
					shared_from_this(), lhs_val, rhs.text, bound_type);
		}
	}

    assert(!status);
    return nullptr;
}

bound_var_t::ref ast::ineq_expr::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	atom function_name;
	switch (token.tk) {
	case tk_lt:
		function_name = "__lt__";
		break;
	case tk_lte:
		function_name = "__lte__";
		break;
	case tk_gt:
		function_name = "__gt__";
		break;
	case tk_gte:
		function_name = "__gte__";
		break;
	default:
		return null_impl();
	}

	return type_check_binary_operator(status, builder, scope, lhs, rhs,
			shared_from_this(), function_name);
}

bound_var_t::ref ast::plus_expr::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	atom function_name;
	switch (token.tk) {
	case tk_plus:
		function_name = "__plus__";
		break;
	case tk_minus:
		function_name = "__minus__";
		break;
	default:
		return null_impl();
	}

	return type_check_binary_operator(status, builder, scope, lhs, rhs,
			shared_from_this(), function_name);
}

bound_var_t::ref call_typeid(
		status_t &status,
		scope_t::ref scope,
		ast::item::ref callsite,
		identifier::ref id,
	   	llvm::IRBuilder<> &builder,
		bound_var_t::ref resolved_value)
{
	debug_above(4, log(log_info, "getting typeid of %s",
				resolved_value->type->str().c_str()));
	auto program_scope = scope->get_program_scope();

	auto llvm_type = resolved_value->type->get_llvm_type();
	auto llvm_obj_type = program_scope->get_bound_type({"__var_ref"})->get_llvm_type();
	bool is_obj = (llvm_type == llvm_obj_type);
	auto name = string_format("typeid(%s)", resolved_value->str().c_str());

	if (is_obj) {
		auto get_typeid_function = program_scope->get_bound_variable(status,
				callsite, "__get_var_type_id");
		if (!!status) {
			return create_callsite(
					status,
					builder,
					scope,
					callsite,
					get_typeid_function,
					name,
					id->get_location(),
					{resolved_value});
		}
	} else {
		return bound_var_t::create(
				INTERNAL_LOC(),
				string_format("typeid(%s)", resolved_value->str().c_str()),
				program_scope->get_bound_type({TYPEID_TYPE}),
				llvm_create_int(builder, resolved_value->type->get_type()->get_signature().iatom),
				id,
				false/*is_lhs*/);
	}

	assert(!status);
	return nullptr;
}


bound_var_t::ref ast::typeid_expr::resolve_instantiation(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
	   	local_scope_t::ref *new_scope,
	   	bool *returns) const
{
	auto resolved_value = expr->resolve_instantiation(status,
			builder,
			scope,
			nullptr,
			returns);

	if (!!status) {
		return call_typeid(status, scope, shared_from_this(), make_code_id(token), builder, resolved_value);
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::sizeof_expr::resolve_instantiation(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
	   	local_scope_t::ref *new_scope,
	   	bool *returns) const
{
	/* calculate the size of the object being referenced assume native types */
	bound_type_t::ref bound_type = upsert_bound_type(status, builder, scope, type);
	bound_type_t::ref size_type = scope->get_program_scope()->get_bound_type({INT_TYPE});
	if (!!status) {
		llvm::Value *llvm_size = llvm_sizeof_type(builder,
				llvm_deref_type(bound_type->get_llvm_type()));

		return bound_var_t::create(
				INTERNAL_LOC(), type->str(), size_type, llvm_size,
				make_iid("sizeof"), false /*is_lhs*/);
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::function_defn::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *) const
{
	llvm::IRBuilderBase::InsertPointGuard ipg(builder);
	assert(!!status);

	/* function definitions are type checked at instantiation points. callsites
	 * are instantiation points.
	 *
	 * The main job of this function is to:
	 * 0. type check the function given the scope.
	 * 1. generate code for this function.
	 * 2. bind the function name to the generated code within the given scope.
	 * */
	indent_logger indent(2, string_format(
				"type checking %s in %s", token.str().c_str(),
				scope->get_name().c_str()));

	/* see if we can get a monotype from the function declaration */
	types::type::ref inbound_context;
	bound_type_t::named_pairs args;
	bound_type_t::ref return_type;
	type_check_fully_bound_function_decl(status, builder, *decl, scope, inbound_context, args, return_type);

	if (!!status) {
		return instantiate_with_args_and_return_type(status, builder, scope,
				new_scope, inbound_context, args, return_type);
	} else {
		user_error(status, *this, "unable to declare function %s due to related errors",
				token.str().c_str());
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::function_defn::instantiate_with_args_and_return_type(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		local_scope_t::ref *new_scope,
		types::type::ref inbound_context,
		bound_type_t::named_pairs args,
		bound_type_t::ref return_type) const
{
	llvm::IRBuilderBase::InsertPointGuard ipg(builder);
	assert(!!status);

	std::string function_name = token.text;

	assert(scope->get_llvm_module() != nullptr);

	auto function_type = get_function_type(inbound_context, args, return_type);
	bound_type_t::ref bound_function_type = upsert_bound_type(status,
			builder, scope, function_type);

	if (!!status) {
		assert(bound_function_type->get_llvm_type() != nullptr);

		llvm::Type *llvm_type = bound_function_type->get_llvm_type();
		if (llvm_type->isPointerTy()) {
			llvm_type = llvm_type->getPointerElementType();
		}
		debug_above(5, log(log_info, "creating function %s with LLVM type %s",
				function_name.c_str(),
				llvm_print_type(*llvm_type).c_str()));
		assert(llvm_type->isFunctionTy());

		llvm::Function *llvm_function = llvm::Function::Create(
				(llvm::FunctionType *)llvm_type,
				llvm::Function::ExternalLinkage, function_name,
				scope->get_llvm_module());

		llvm::BasicBlock *llvm_block = llvm::BasicBlock::Create(builder.getContext(), "entry", llvm_function);
		builder.SetInsertPoint(llvm_block);

		/* set up the mapping to this function for use in recursion */
		bound_var_t::ref function_var = bound_var_t::create(
				INTERNAL_LOC(), token.text, bound_function_type, llvm_function,
				make_code_id(token), false/*is_lhs*/);

		/* we should be able to check its block as a callsite. note that this
		 * code will also run for generics but only after the
		 * sbk_generic_substitution mechanism has run its course. */
		auto params_scope = make_param_list_scope(status, builder, *decl, scope,
				function_var, args);

		/* now put this function declaration into the containing scope in case
		 * of indirect recursion */
		if (function_var->name.size() != 0) {
			/* inline function definitions are scoped to the virtual block in which
			 * they appear */
			if (auto local_scope = dyncast<local_scope_t>(scope)) {
				*new_scope = local_scope->new_local_scope(
						string_format("function-%s", function_name.c_str()));

				(*new_scope)->put_bound_variable(status, function_var->name, function_var);
			} else {
				module_scope_t::ref module_scope = dyncast<module_scope_t>(scope);

				if (module_scope == nullptr) {
					if (auto subst_scope = dyncast<generic_substitution_scope_t>(scope)) {
						module_scope = dyncast<module_scope_t>(subst_scope->get_parent_scope());
					}
				}

				if (module_scope != nullptr) {
					/* before recursing directly or indirectly, let's just add
					 * this function to the module scope we're in */
					scope->get_program_scope()->put_bound_variable(status, function_var->name, function_var);
					if (!!status) {
						module_scope->mark_checked(status, builder,
								shared_from_this());
						assert(!!status);
					}
				}
			}
		} else {
			user_error(status, *this, "function definitions need names");
		}

		if (!!status) {
			/* keep track of whether this function returns */
			bool all_paths_return = false;
			params_scope->return_type_constraint = return_type;
			block->resolve_instantiation(status, builder, params_scope,
					nullptr, &all_paths_return);

			if (!!status) {
				debug_above(10, log(log_info, "module dump from %s\n%s",
							__PRETTY_FUNCTION__,
							llvm_print_module(*llvm_get_module(builder)).c_str()));

				if (all_paths_return) {
					return function_var;
				} else {
					/* not all control paths return */
					if (return_type->is_void()) {
						/* if this is a void let's give the user a break and insert
						 * a default void return */
						builder.CreateRetVoid();
						return function_var;
					} else {
						/* no breaks here, we don't know what to return */
						user_error(status, *this, "not all control paths return a value");
					}
				}

				llvm_verify_function(status, llvm_function);
			}
		}
	}

    assert(!status);
    return nullptr;
}

status_t type_check_module_links(
        compiler &compiler,
        llvm::IRBuilder<> &builder,
        const ast::module &obj,
        scope_t::ref program_scope)
{
	indent_logger indent(3, string_format("resolving links in " c_module("%s"),
				obj.module_key.c_str()));

	status_t status;

	/* get module level scope variable */
	module_scope_t::ref scope = compiler.get_module_scope(obj.module_key);

	for (auto &link : obj.linked_modules) {
		link->resolve_instantiation(status, builder, scope, nullptr, nullptr);
	}

	if (!!status) {
		for (auto &link : obj.linked_functions) {
			bound_var_t::ref link_value = link->resolve_instantiation(
					status, builder, scope, nullptr, nullptr);

			if (!!status) {
				if (link->function_name.text.size() != 0) {
					scope->put_bound_variable(status, link->function_name.text, link_value);
				} else {
					user_error(status, *link, "module level link definitions need names");
				}
			}
		}
	}

	return status;
}

status_t type_check_module_types(
        compiler &compiler,
        llvm::IRBuilder<> &builder,
        const ast::module &obj,
        scope_t::ref program_scope)
{
	indent_logger indent(2, string_format("type-checking types in module " c_module("%s"),
				obj.module_key.str().c_str()));
	status_t final_status;

	/* get module level scope types */
	module_scope_t::ref module_scope = compiler.get_module_scope(obj.module_key);

	auto unchecked_types_ordered = module_scope->get_unchecked_types_ordered();
	for (int i = 0; i < unchecked_types_ordered.size(); ++i) {
		auto unchecked_type = unchecked_types_ordered[i];
		auto node = unchecked_type->node;
		if (!module_scope->has_checked(node)) {
			assert(!dyncast<const ast::function_defn>(node));

			/* prevent recurring checks */
			debug_above(5, log(log_info, "checking module level type %s", node->token.str().c_str()));

			/* these next lines create type definitions, regardless of
			 * their genericity.  type expressions will be added as
			 * environment variables in the type system.  this step is
			 * MUTATING the type environment of the module, and the
			 * program. */
			if (auto type_def = dyncast<const ast::type_def>(node)) {
				status_t status;
				type_def->resolve_instantiation(
						status, builder, module_scope, nullptr, nullptr);

				/* take note of whether this failed or not */
				final_status |= status;
			} else if (auto tag = dyncast<const ast::tag>(node)) {
				status_t status;
				tag->resolve_instantiation(
						status, builder, module_scope, nullptr, nullptr);

				/* take note of whether this failed or not */
				final_status |= status;
			} else {
				assert(!"unhandled unchecked type node at module scope");
			}
		} else {
			debug_above(3, log(log_info, "skipping %s because it's already been checked", node->token.str().c_str()));
        }
    }

    return final_status;
}

status_t type_check_program_variables(
        compiler &compiler,
        llvm::IRBuilder<> &builder,
        program_scope_t::ref program_scope)
{
	indent_logger indent(2, string_format("resolving variables in program"));

	status_t final_status;

	auto unchecked_vars_ordered = program_scope->get_unchecked_vars_ordered();
    for (int i = 0; i < unchecked_vars_ordered.size(); ++i) {
		status_t status;

		auto &unchecked_var = unchecked_vars_ordered[i];
		debug_above(5, log(log_info, "checking whether to check %s",
					unchecked_var->str().c_str()));

		auto node = unchecked_var->node;
		if (!unchecked_var->module_scope->has_checked(node)) {
			/* prevent recurring checks */
			debug_above(4, log(log_info, "checking module level variable %s",
					   	node->token.str().c_str()));
			if (auto function_defn = dyncast<const ast::function_defn>(node)) {
				// TODO: decide whether we need treatment here
				if (is_function_defn_generic(status, builder,
							unchecked_var->module_scope, *function_defn))
			   	{
					/* this is a generic function, or we've already checked
					 * it so let's skip checking it */
					final_status |= status;
					continue;
				}
			}

			if (!!status) {
				if (auto stmt = dyncast<const ast::statement>(node)) {
					status_t status;
					bound_var_t::ref variable = stmt->resolve_instantiation(
							status, builder, unchecked_var->module_scope,
							nullptr, nullptr);

					/* take note of whether this failed or not */
					final_status |= status;
				} else if (auto data_ctor = dyncast<const ast::type_product>(node)) {
					/* ignore until instantiation at a callsite */
				} else {
					assert(!"unhandled unchecked node at module scope");
				}
			}
		} else {
			debug_above(3, log(log_info, "skipping %s because it's already been checked", node->token.str().c_str()));
		}
    }

    return final_status;
}

status_t type_check_program(
        llvm::IRBuilder<> &builder,
        const ast::program &obj,
        compiler &compiler)
{
	indent_logger indent(2, string_format(
				"type-checking program %s",
				compiler.get_program_name().c_str()));

	/* we track type-checking success or failure in this status value object */
	status_t status;

	ptr<program_scope_t> program_scope = compiler.get_program_scope();
	debug_above(11, log(log_info, "type_check_program program scope:\n%s", program_scope->str().c_str()));

	/* pass to resolve all module-level types */
	for (auto &module : obj.modules) {
		if (!status) {
			break;
		}
		status |= type_check_module_types(compiler, builder, *module, program_scope);
	}

	/* pass to resolve all module-level links */
	for (auto &module : obj.modules) {
		if (!status) {
			break;
		}
		status |= type_check_module_links(compiler, builder, *module, program_scope);
	}

	if (!status) {
		return status;
	}

	assert(compiler.main_module != nullptr);

	/* pass to resolve all main module-level variables.  technically we only
	 * need to check the primary module, since that is the one that is expected
	 * to have the entry point ... at least for now... */
	return type_check_program_variables(compiler, builder, program_scope);
}

bound_var_t::ref ast::tag::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool * /*returns*/) const
{
	auto id = make_code_id(token);
	atom tag_name = id->get_name();
	auto tag_type = type_id(id);

	/* it's a nullary enumeration or "tag", let's create a global value to
	 * represent this tag. */

	if (!!status) {
		/* start by making a type for the tag */
		bound_type_t::ref bound_tag_type = bound_type_t::create(
				tag_type,
				id->get_location(),
				/* all tags use the var_t* type */
				scope->get_program_scope()->get_bound_type({"__var_ref"})->get_llvm_type());

		scope->get_program_scope()->put_bound_type(status, bound_tag_type);
		if (!!status) {
			bound_var_t::ref tag = llvm_create_global_tag(
					builder, scope, bound_tag_type, tag_name, id);

			/* record this tag variable for use later */
			scope->put_bound_variable(status, tag_name, tag);

			if (!!status) {
				debug_above(7, log(log_info, "instantiated nullary data ctor %s",
							tag->str().c_str()));
				return tag;
			}
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::type_def::resolve_instantiation(
		status_t &status,
		llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool * /*returns*/) const
{
	/* the goal of this function is to
	 * construct a type, and its requisite parts - not limited to type
	 * definition - such as ctors, accessors, etc, and instantiate those
	 * components into the eligible scopes.  the current type we're defining
	 * should provide a definition that is defined in terms of fully qualified
	 * names.  the type will eventually be able to be referenced by its
	 * name. types can be imported across module boundaries, and type
	 * definitions can be generic in declaration, but concrete in resolution.
	 * this function is the declaration step. */

	atom type_name = type_decl->token.text;
	auto already_bound_type = scope->get_bound_type(type_name);
	if (already_bound_type != nullptr) {
		debug_above(1, log(log_warning, "found predefined bound type for %s -> %s",
			   	type_decl->token.str().c_str(),
				already_bound_type->str().c_str()));
		
		// this is probably fine in practice, but maybe we should check whether
		// the already existing type was created in this scope
		dbg();
	}

	if (auto runnable_scope = dyncast<runnable_scope_t>(scope)) {
		assert(new_scope != nullptr);

		/* type definitions begin new scopes */
		local_scope_t::ref fresh_scope = runnable_scope->new_local_scope(
				string_format("type-%s", token.text.c_str()));

		/* update current scope for writing */
		scope = fresh_scope;

		/* have the caller update their current scope */
		*new_scope = fresh_scope;
	}

	// TODO: consider type namespacing here, or 
	type_algebra->register_type(status, builder,
			make_code_id(token), type_decl->type_variables, scope);

	return nullptr;
}

bound_var_t::ref type_check_assignment(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_var_t::ref lhs_var,
		bound_var_t::ref rhs_var,
		struct location location)
{
	if (!!status) {
		indent_logger indent(5, string_format(
					"type checking assignment %s = %s",
					lhs_var->str().c_str(),
					rhs_var->str().c_str()));

		if (lhs_var->is_lhs) {
			// TODO: load and queue up a free of whatever the LHS is currently pointing at

			unification_t unification = unify(
					lhs_var->type->get_type(),
					rhs_var->type->get_type(), scope->get_typename_env());

			if (!!status) {
				if (unification.result) {
					if (llvm::AllocaInst *llvm_alloca = llvm::dyn_cast<llvm::AllocaInst>(lhs_var->llvm_value)) {
						builder.CreateStore(llvm_resolve_alloca(builder, rhs_var->llvm_value), llvm_alloca);
						return lhs_var;
					} else {
						assert(false);
					}
				} else {
					user_error(status, location, "left-hand side is incompatible with the right-hand side (%s)",
							unification.str().c_str());
				}
			}
		} else {
			user_error(status, location, "left-hand side of assignment is not mutable");
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::assignment::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	assert(token.text == "=");

	auto lhs_var = lhs->resolve_instantiation(status, builder, scope, nullptr, nullptr);
	if (!!status) {
		auto rhs_var = rhs->resolve_instantiation(status, builder, scope, nullptr, nullptr);
		return type_check_assignment(status, builder, scope, lhs_var, rhs_var, token.location);
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::break_flow::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	if (auto runnable_scope = dyncast<runnable_scope_t>(scope)) {
		llvm::BasicBlock *break_bb = runnable_scope->get_innermost_loop_break();
		if (break_bb != nullptr) {
			assert(!builder.GetInsertBlock()->getTerminator());
			builder.CreateBr(break_bb);
			return nullptr;
		} else {
			user_error(status, get_location(), c_control("break") " outside of a loop");
		}
	} else {
		panic("we should not be looking at a break statement here!");
	}
	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::continue_flow::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	if (auto runnable_scope = dyncast<runnable_scope_t>(scope)) {
		llvm::BasicBlock *continue_bb = runnable_scope->get_innermost_loop_continue();
		if (continue_bb != nullptr) {
			assert(!builder.GetInsertBlock()->getTerminator());
			builder.CreateBr(continue_bb);
			return nullptr;
		} else {
			user_error(status, get_location(), c_control("continue") " outside of a loop");
		}
	} else {
		panic("we should not be looking at a continue statement here!");
	}
	assert(!status);
	return nullptr;
}

bound_var_t::ref type_check_binary_op_assignment(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		ast::item::ref op_node,
		ast::statement::ref lhs,
		ast::statement::ref rhs,
		struct location location,
		atom function_name)
{
	auto lhs_var = lhs->resolve_instantiation(status, builder, scope, nullptr,
			nullptr);

	if (!!status) {
		auto rhs_var = rhs->resolve_instantiation(status, builder, scope,
				nullptr, nullptr);

		if (!!status) {
			auto computed_var = call_program_function(status, builder, scope,
					function_name, op_node, {lhs_var, rhs_var});

			return type_check_assignment(status, builder, scope, lhs_var,
					computed_var, location);
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::mod_assignment::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	return type_check_binary_op_assignment(status, builder, scope,
			shared_from_this(), lhs, rhs, token.location, "__mod__");
}

bound_var_t::ref ast::plus_assignment::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	return type_check_binary_op_assignment(status, builder, scope,
			shared_from_this(), lhs, rhs, token.location, "__plus__");
}

bound_var_t::ref ast::minus_assignment::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	return type_check_binary_op_assignment(status, builder, scope,
			shared_from_this(), lhs, rhs, token.location, "__minus__");
}

bound_var_t::ref ast::return_statement::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	/* obviously... */
	*returns = true;

	/* let's figure out if we have a return value, and what it's type is */
    bound_var_t::ref return_value;
    bound_type_t::ref return_type;

    if (expr) {
        /* if there is a return expression resolve it into a value */
        return_value = expr->resolve_instantiation(status, builder, scope, nullptr, nullptr);
        if (!!status) {
            /* get the type suggested by this return value */
            return_type = return_value->type;
        }
    } else {
        /* we have an empty return, let's just use void */
        return_type = scope->get_program_scope()->get_bound_type({"void"});
    }

    if (!!status) {
        runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
        assert(runnable_scope != nullptr);

		/* make sure this return type makes sense, or keep track of it if we
		 * didn't yet know the return type for this function */
		runnable_scope->check_or_update_return_type_constraint(status,
				shared_from_this(), return_type);

		if (return_value != nullptr) {
			builder.CreateRet(llvm_resolve_alloca(builder, return_value->llvm_value));
		} else {
			assert(types::is_type_id(return_type->get_type(), "void"));
			builder.CreateRetVoid();
		}

        return return_value;
    }
    assert(!status);
    return nullptr;
}

bound_var_t::ref ast::times_assignment::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	return type_check_binary_op_assignment(status, builder, scope,
			shared_from_this(), lhs, rhs, token.location, "__times__");
}

bound_var_t::ref ast::divide_assignment::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	return type_check_binary_op_assignment(status, builder, scope,
			shared_from_this(), lhs, rhs, token.location, "__divide__");
}

bound_var_t::ref ast::block::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns_) const
{
	/* it's important that we keep track of returns */
	bool placeholder_returns = false;
	bool *returns = returns_;
	if (returns == nullptr) {
		returns = &placeholder_returns;
	}

    scope_t::ref current_scope = scope;

	assert(builder.GetInsertBlock() != nullptr);

	for (auto &statement : statements) {
		if (*returns) {
			user_error(status, *statement, "this statement will never run");
		}

		local_scope_t::ref next_scope;

		debug_above(9, log(log_info, "type checking statement\n%s", statement->str().c_str()));

		statement->resolve_instantiation(status, builder, current_scope,
				&next_scope, returns);

		if (!!status) {
			if (next_scope != nullptr) {
				/* the statement just executed wants to create a new nested scope.
				 * let's allow this by just keeping track of the current scope. */
				current_scope = next_scope;
				next_scope = nullptr;
				debug_above(10, log(log_info, "got a new scope %s", current_scope->str().c_str()));
			}
		} else {
			if (!status.reported_on_error_at(statement->get_location())) {
				user_error(status, statement->get_location(), "while checking %s",
						statement->str().c_str());
			}
			break;
		}
    }

    /* blocks don't really have values */
    return nullptr;
}

llvm::Value *get_raw_condition_value(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		ast::item::ref condition,
		bound_var_t::ref condition_value)
{
	if (condition_value->is_int()) {
		return llvm_resolve_alloca(builder, condition_value->llvm_value);
	} else if (condition_value->is_pointer()) {
		return llvm_resolve_alloca(builder, condition_value->llvm_value);
	} else {
		user_error(status, condition->get_location(), "unknown basic type: %s",
				condition_value->str().c_str());
	}

	assert(!status);
	return nullptr;
}

llvm::Value *maybe_get_bool_overload_value(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		ast::item::ref condition,
		bound_var_t::ref condition_value)
{
	llvm::Value *llvm_condition_value = nullptr;
	// TODO: check whether we are checking a raw value or not

	debug_above(2, log(log_info,
				"attempting to resolve a " c_var("%s") " override if condition %s, ",
				BOOL_TYPE,
				condition->str().c_str()));

	/* we only ever get in here if we are definitely non-null, so we can discard
	 * maybe type specifiers */
	types::type::ref condition_type;
	if (auto maybe = dyncast<const types::type_maybe>(condition_value->type->get_type())) {
		condition_type = maybe->just;
	} else {
		condition_type = condition_value->type->get_type();
	}

	var_t::refs fns;
	auto bool_fn = maybe_get_callable(status, builder, scope, BOOL_TYPE,
			condition, scope->get_outbound_context(),
			get_args_type({condition_type}), fns);

	if (!!status) {
		if (bool_fn != nullptr) {
			/* we've found a bool function that will take our condition as input */
			assert(bool_fn != nullptr);

			if (get_function_return_type(bool_fn->type->get_type())->get_signature() == "__bool__") {
				debug_above(7, log(log_info, "generating a call to " c_var("bool") "(%s) for if condition evaluation (type %s)",
							condition->str().c_str(), bool_fn->type->str().c_str()));

				/* let's call this bool function */
				llvm_condition_value = llvm_create_call_inst(
						status, builder, *condition, bool_fn,
						{condition_value->llvm_value});
				if (!!status) {
					assert(llvm_condition_value->getType()->isIntegerTy());
					return llvm_condition_value;
				}
			} else {
				user_error(status, bool_fn->get_location(),
						"__bool__ coercion function must return a " C_TYPE "__bool__" C_RESET);
				user_error(status, bool_fn->get_location(),
						"implicit __bool__ was defined function must return a " C_TYPE "__type__" C_RESET);
			}
		} else {
			/* treat all values without overloaded bool functions as truthy */
			return nullptr;
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::while_block::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	/* while scope allows us to set up new variables inside while conditions */
	local_scope_t::ref while_scope;

	if (condition != nullptr) {
		assert(token.text == "while");

		llvm::Function *llvm_function_current = llvm_get_function(builder);

		llvm::BasicBlock *while_cond_bb = llvm::BasicBlock::Create(builder.getContext(), "while.cond", llvm_function_current);

		assert(!builder.GetInsertBlock()->getTerminator());
		builder.CreateBr(while_cond_bb);
		builder.SetInsertPoint(while_cond_bb);

		/* evaluate the condition for branching */
		bound_var_t::ref condition_value = condition->resolve_instantiation(
				status, builder, scope, &while_scope, nullptr);

		if (!!status) {
			debug_above(5, log(log_info,
						"getting raw condition for value %s",
						condition_value->str().c_str()));
			llvm::Value *llvm_raw_condition_value = get_raw_condition_value(status,
					builder, scope, condition, condition_value);

			if (!!status) {
				assert(llvm_raw_condition_value != nullptr);

				/* generate some new blocks */
				llvm::BasicBlock *while_block_bb = llvm::BasicBlock::Create(builder.getContext(), "while.block", llvm_function_current);
				llvm::BasicBlock *while_end_bb = nullptr;

				/* put the merge block after the while block */
				while_end_bb = llvm::BasicBlock::Create(builder.getContext(), "while.end");

				/* keep track of the "break" and "continue" jump locations */
				loop_tracker_t loop_tracker(dyncast<runnable_scope_t>(scope), while_cond_bb, while_end_bb);

				/* we don't have an else block, so we can just continue on */
				llvm_create_if_branch(builder, llvm_raw_condition_value, while_block_bb, while_end_bb);

				if (!!status) {
					/* let's generate code for the "then" block */
					builder.SetInsertPoint(while_block_bb);

					llvm::Value *llvm_bool_overload_value = maybe_get_bool_overload_value(status,
							builder, scope, condition, condition_value);

					if (!!status) {
						if (llvm_bool_overload_value != nullptr) {
							/* we've got a second condition to check, let's do it */
							auto deep_while_bb = llvm::BasicBlock::Create(builder.getContext(), "deep-while", llvm_function_current);

							llvm_create_if_branch(builder, llvm_bool_overload_value,
									deep_while_bb, while_end_bb);
							builder.SetInsertPoint(deep_while_bb);
						}
					}

					block->resolve_instantiation(status, builder,
							while_scope ? while_scope : scope, nullptr,
							nullptr);

					if (!!status) {
						if (!builder.GetInsertBlock()->getTerminator()) {
							builder.CreateBr(while_cond_bb);
						}
						builder.SetInsertPoint(while_end_bb);
						
						/* we know we'll need to fall through to the merge
						 * block, let's add it to the end of the function
						 * and let's set it as the next insert point. */
						llvm_function_current->getBasicBlockList().push_back(while_end_bb);
						builder.SetInsertPoint(while_end_bb);

						assert(!!status);
						return nullptr;
					}
				}
			}
		}
	} else {
		/* this should never happen */
		not_impl();
	}

    assert(!status);
    return nullptr;
}

bound_var_t::ref ast::if_block::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	/* if scope allows us to set up new variables inside if conditions */
	local_scope_t::ref if_scope;

	bool if_block_returns = false, else_block_returns = false;

	assert(condition != nullptr);

	assert(token.text == "if" || token.text == "elif");
	bound_var_t::ref condition_value;

	/* evaluate the condition for branching */
	if (auto var_decl = dyncast<const ast::var_decl>(condition)) {
		/* our user is attempting an assignment inside of an if statement, let's
		 * grant them a favor, and automatically unbox the Maybe type if it
		 * exists. */
		condition_value = var_decl->resolve_as_condition(
				status, builder, scope, &if_scope);
	} else if (auto ref_expr = dyncast<const ast::reference_expr>(condition)) {
		condition_value = ref_expr->resolve_as_condition(
				status, builder, scope, &if_scope);
	} else {
		condition_value = condition->resolve_instantiation(
				status, builder, scope, &if_scope, nullptr);
	}

		/*
		 * var maybe_vector Vector? = maybe_a_vector()
		 *
		 * if v := maybe_vector
		 *   print("x-value is " + v.x)
		 * else
		 *   print("no x-value available")
		 *
		 * if nil is a subtype of maybe_vector, then the above code
		 * effectively becomes:
		 *
		 * if __not_nil__(maybe_vector)
		 *   v := __discard_nil__(maybe_vector)
		 *   // if there is a __bool__ function defined for type(v), add another
		 *   // if statement:
		 *   if not v
		 *     goto l_else
		 *   print("x-axis is " + v.x)
		 * else
		 * l_else:
		 *   print("no x-value available")
		 *
		 * if nil is not a subtype of maybe_vector, for example, for a Vector
		 * class, 
		 */

	if (!!status) {
		/* if the condition value is a maybe type, then we'll need multiple
		 * anded conditions to be true in order to actuall fall into the then
		 * block, let's figure out those conditions */
		llvm::Value *llvm_raw_condition_value = get_raw_condition_value(status,
				builder, scope, condition, condition_value);

		if (!!status && llvm_raw_condition_value != nullptr) {
			/* test that the if statement doesn't return */
			llvm::Function *llvm_function_current = llvm_get_function(builder);

			/* generate some new blocks */
			llvm::BasicBlock *then_bb = llvm::BasicBlock::Create(builder.getContext(), "then", llvm_function_current);
			llvm::BasicBlock *merge_bb = nullptr;

			/* we have to keep track of whether we need a merge block
			 * because our nested branches could all return */
			bool insert_merge_bb = false;

			llvm::BasicBlock *else_bb = nullptr;
			if (else_ != nullptr) {
				/* we've got an else block, so let's create an "else" basic block. */
				else_bb = llvm::BasicBlock::Create(builder.getContext(), "else", llvm_function_current);

				/* put the merge block after the else block */
				merge_bb = llvm::BasicBlock::Create(builder.getContext(), "ifcont");

				/* create the actual branch instruction */
				llvm_create_if_branch(builder, llvm_raw_condition_value, then_bb, else_bb);

				builder.SetInsertPoint(else_bb);
				else_->resolve_instantiation(status, builder, scope, nullptr, &else_block_returns);

				if (!else_block_returns) {
					/* keep track of the fact that we have to have a
					 * merged block to land in after the else block */
					insert_merge_bb = true;

					/* go ahead and jump there */
					if (!builder.GetInsertBlock()->getTerminator()) {
						builder.CreateBr(merge_bb);
					}
				}
			} else {
				/* since there is no else block it cannot return */
				else_block_returns = false;

				/* keep track of the fact that we have to have a merged
				 * block to land in after the if block */
				insert_merge_bb = true;

				/* put the merge block after the if block */
				merge_bb = llvm::BasicBlock::Create(builder.getContext(), "ifcont");

				/* we don't have an else block, so we can just continue on */
				llvm_create_if_branch(builder, llvm_raw_condition_value, then_bb, merge_bb);
			}

			if (!!status) {
				/* let's generate code for the "then" block */
				builder.SetInsertPoint(then_bb);
				llvm::Value *llvm_bool_overload_value = maybe_get_bool_overload_value(status,
						builder, scope, condition, condition_value);

				if (!!status) {
					if (llvm_bool_overload_value != nullptr) {
						/* we've got a second condition to check, let's do it */
						auto deep_then_bb = llvm::BasicBlock::Create(builder.getContext(), "deep-then", llvm_function_current);

						llvm_create_if_branch(builder, llvm_bool_overload_value,
								deep_then_bb, else_bb ? else_bb : merge_bb);
						builder.SetInsertPoint(deep_then_bb);
					}

					block->resolve_instantiation(status, builder,
						   	if_scope ? if_scope : scope, nullptr, &if_block_returns);
					if (!!status) {
						if (!if_block_returns) {
							insert_merge_bb = true;
							if (!builder.GetInsertBlock()->getTerminator()) {
								builder.CreateBr(merge_bb);
							}
							builder.SetInsertPoint(merge_bb);
						}

						if (insert_merge_bb) {
							/* we know we'll need to fall through to the merge
							 * block, let's add it to the end of the function
							 * and let's set it as the next insert point. */
							llvm_function_current->getBasicBlockList().push_back(merge_bb);
							builder.SetInsertPoint(merge_bb);
						}

						/* track whether the branches return */
						*returns |= (if_block_returns && else_block_returns);

						assert(!!status);
						return nullptr;
					}
				}
			}
		}
	}

	assert(!status);
    return nullptr;
}

bound_var_t::ref ast::bang_expr::resolve_instantiation(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
	   	local_scope_t::ref *new_scope,
	   	bool *) const
{
	auto lhs_value = lhs->resolve_instantiation(status, builder, scope, new_scope, nullptr);
	if (!!status) {
		auto type = lhs_value->type->get_type();
		auto maybe_type = dyncast<const types::type_maybe>(type);
		if (maybe_type != nullptr) {
			bound_type_t::ref just_bound_type = upsert_bound_type(status, builder, scope, maybe_type->just);
			return bound_var_t::create(INTERNAL_LOC(), lhs_value->name,
					just_bound_type,
					lhs_value->llvm_value,
					lhs_value->id,
					lhs_value->is_lhs);
		} else {
			user_error(status, *this, "bang expression is unnecessary since this is not a 'maybe' type: %s",
					type->str().c_str());
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::var_decl::resolve_as_condition(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
	   	local_scope_t::ref *new_scope) const
{
    runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
    assert(runnable_scope);

    /* variable declarations begin new scopes */
    local_scope_t::ref fresh_scope = runnable_scope->new_local_scope(
            string_format("if-assignment-%s", token.text.c_str()));

    scope = fresh_scope;

    /* check to make sure this var decl is sound */
    bound_var_t::ref var_decl_value = type_check_bound_var_decl(
            status, builder, *this, fresh_scope, true /*maybe_unbox*/);

	if (!!status) {
		*new_scope = fresh_scope;
		return var_decl_value;
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::var_decl::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool * /*returns*/) const
{
    runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
    assert(runnable_scope);

    /* variable declarations begin new scopes */
    local_scope_t::ref fresh_scope = runnable_scope->new_local_scope(
            string_format("variable-%s", token.text.c_str()));

    scope = fresh_scope;

    /* check to make sure this var decl is sound */
    bound_var_t::ref var_decl_value = type_check_bound_var_decl(
            status, builder, *this, fresh_scope, false /*maybe_unbox*/);

	if (!!status) {
		*new_scope = fresh_scope;
		return var_decl_value;
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::pass_flow::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
    return nullptr;
}

bound_var_t::ref ast::times_expr::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	atom function_name;
	switch (token.tk) {
	case tk_times:
		function_name = "__times__";
		break;
	case tk_divide_by:
		function_name = "__divide__";
		break;
	case tk_mod:
		function_name = "__mod__";
		break;
	default:
		return null_impl();
	}

	return type_check_binary_operator(status, builder, scope, lhs, rhs,
			shared_from_this(), function_name);
}

bound_var_t::ref ast::prefix_expr::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	atom function_name;
	switch (token.tk) {
	case tk_minus:
		function_name = "__negative__";
		break;
	case tk_plus:
		function_name = "__positive__";
		break;
	case tk_not:
		function_name = "__not__";
		break;
	default:
		return null_impl();
	}

    /* first solve the right hand side */
    bound_var_t::ref rhs_var = rhs->resolve_instantiation(status, builder, scope, nullptr, nullptr);

    if (!!status) {
        return call_program_function(status, builder, scope, function_name,
                shared_from_this(), {rhs_var});
    }
    assert(!status);
    return nullptr;
}

bound_var_t::ref ast::literal_expr::resolve_instantiation(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
    scope_t::ref program_scope = scope->get_program_scope();

    switch (token.tk) {
    case tk_integer:
        {
			/* create a boxed integer */
            int64_t value = atoll(token.text.c_str());
            bound_type_t::ref raw_type = program_scope->get_bound_type({INT_TYPE});
			bound_type_t::ref boxed_type = upsert_bound_type(
					status,
					builder,
					scope,
					type_id(make_iid("int")));
			if (!!status) {
				assert(boxed_type != nullptr);
				bound_var_t::ref box_int = get_callable(
						status,
						builder,
						scope,
						{"int"},
						shared_from_this(),
						scope->get_outbound_context(),
						get_args_type({raw_type}));

				if (!!status) {
					assert(box_int != nullptr);
					return create_callsite(
							status,
							builder,
							scope,
							shared_from_this(),
							box_int,
							{string_format("literal int (%d)", value)},
							get_location(),
							{bound_var_t::create(
									INTERNAL_LOC(), "temp_int_literal", boxed_type,
									llvm_create_int(builder, value),
									make_code_id(token), false/*is_lhs*/)});
				}
			}
        }
		break;
    case tk_string:
		{
			std::string value = unescape_json_quotes(token.text);
			bound_type_t::ref raw_type = program_scope->get_bound_type({STR_TYPE});
			bound_type_t::ref boxed_type = upsert_bound_type(
					status,
					builder,
					scope,
					type_id(make_iid("str")));

			if (!!status) {
				assert(boxed_type != nullptr);
				bound_var_t::ref box_str = get_callable(
						status,
						builder,
						scope,
						{"str"},
						shared_from_this(),
						scope->get_outbound_context(),
						get_args_type({raw_type}));

				if (!!status) {
					return create_callsite(
							status,
							builder,
							scope,
							shared_from_this(),
							box_str,
							{string_format("literal str (%s)", value.c_str())},
							get_location(),
							{bound_var_t::create(
									INTERNAL_LOC(), "temp_str_literal", boxed_type,
									llvm_create_global_string(builder, value),
									make_code_id(token), false/*is_lhs*/)});
				}
			}
		}
		break;
	case tk_float:
		{
			float value = atof(token.text.c_str());
			bound_type_t::ref raw_type = program_scope->get_bound_type({FLOAT_TYPE});
			bound_type_t::ref boxed_type = upsert_bound_type(
					status,
					builder,
					scope,
					type_id(make_iid("float")));
			if (!!status) {
				assert(boxed_type != nullptr);
				bound_var_t::ref box_float = get_callable(
						status,
						builder,
						scope,
						{"float"},
						shared_from_this(),
						scope->get_outbound_context(),
						get_args_type({raw_type}));

				if (!!status) {
					return create_callsite(
							status,
							builder,
							scope,
							shared_from_this(),
							box_float,
							{string_format("literal float (%f)", value)},
							get_location(),
							{bound_var_t::create(
									INTERNAL_LOC(), "temp_float_literal", boxed_type,
									llvm_create_float(builder, value),
									make_code_id(token), false/*is_lhs*/)});
				}
			}
		}
		break;
    default:
        assert(false);
    };

    assert(!status);
    return nullptr;
}

bound_var_t::ref ast::reference_expr::resolve_overrides(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		const ptr<const ast::item> &callsite,
		const bound_type_t::refs &args) const
{
	indent_logger indent(5, string_format(
				"reference_expr::resolve_overrides for %s",
				callsite->str().c_str()));

	/* ok, we know we've got some variable here */
	auto bound_var = get_callable(status, builder, scope, token.text,
			shared_from_this(), scope->get_outbound_context(),
			get_args_type(args));
	if (!!status) {
		return bound_var;
	} else {
		user_error(status, callsite->get_location(), "while checking %s with %s",
				callsite->str().c_str(),
				::str(args).c_str());
		return nullptr;
	}
}

bound_var_t::ref ast::cast_expr::resolve_instantiation(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
	   	local_scope_t::ref *new_scope,
	   	bool *returns) const
{
	bound_var_t::ref bound_var = lhs->resolve_instantiation(status, builder, scope, nullptr, nullptr);
	if (!!status) {

		if (!!status) {
			bound_type_t::ref bound_type = upsert_bound_type(status, builder, scope, type_cast);
			if (!!status) {
				llvm::Value *llvm_var_value = llvm_resolve_alloca(builder, bound_var->llvm_value);
				llvm::Value *llvm_value_as_specific_type = builder.CreatePointerBitCastOrAddrSpaceCast(
						llvm_var_value, bound_type->get_llvm_type());

				return bound_var_t::create(INTERNAL_LOC(), "cast",
					bound_type, llvm_value_as_specific_type, make_iid("cast"),
					false /*is_lhs*/);
			}
		}
	}

	assert(!status);
	return nullptr;
}
