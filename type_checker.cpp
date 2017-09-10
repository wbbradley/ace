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
		const ast::var_decl_t &obj,
		scope_t::ref scope,
		atom &var_name,
		atom::set &generics,
		int &generic_index)
{
	if (!!status) {
		/* get the name of this parameter */
		var_name = obj.token.text;

		assert(obj.type != nullptr);

		/* the user specified a type */
		if (!!status) {
			debug_above(6, log(log_info, "upserting type for param %s at %s",
						obj.type->str().c_str(),
						obj.type->get_location().str().c_str()));
			return upsert_bound_type(status, builder, scope, obj.type);
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref generate_stack_variable(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		const ast::like_var_decl_t &obj,
		atom symbol,
		types::type_t::ref declared_type,
		bool maybe_unbox)
{
	/* 'init_var' is keeping track of the value we are assigning to our new
	 * variable (if any exists.) */
	bound_var_t::ref init_var;

	/* only check initializers inside a runnable scope */
	assert(dyncast<runnable_scope_t>(scope) != nullptr);

	if (obj.has_initializer()) {
		/* we have an initializer */
		init_var = obj.resolve_initializer(status, builder, scope, life);
	}

	/* 'type' is keeping track of what the variable's ending type will be */
	bound_type_t::ref stack_var_type;
	bound_type_t::ref value_type;
	types::type_t::ref lhs_type = declared_type;

	/* 'unboxed' tracks whether we are doing maybe unboxing for this var_decl */
	bool unboxed = false;

	if (!!status) {
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
					user_error(status, obj.get_location(),
							"declared type of `" c_var("%s") "` does not match type of initializer",
							obj.get_symbol().c_str());
					user_message(log_info, status, init_var->get_location(), c_type("%s") " != " c_type("%s"),
							declared_type->str().c_str(),
							init_var->type->str().c_str());
				}
			} else {
				/* we must get the type from the initializer */
				lhs_type = init_var->type->get_type();
			}
		}

		assert(lhs_type != nullptr);

		if (maybe_unbox) {
			debug_above(3, log(log_info, "attempting to unbox %s", obj.get_symbol().c_str()));

			/* try to see if we can unbox this if it's a Maybe */
			if (init_var == nullptr) {
				user_error(status, obj.get_location(), "missing initialization value");
			} else {
				/* since we are maybe unboxing, then let's first off see if
				 * this is even a maybe type. */
				if (auto maybe_type = dyncast<const types::type_maybe_t>(lhs_type)) {
					/* looks like the initialization variable is a supertype
					 * of the nil type */
					unboxed = true;

					stack_var_type = upsert_bound_type(status, builder, scope,
							type_ref(maybe_type->just));
					if (!!status) {
						value_type = upsert_bound_type(status, builder, scope, maybe_type->just);
					}
				} else {
					/* this is not a maybe, so let's just move along */
				}
			}
		}

		if (stack_var_type == nullptr) {
			stack_var_type = upsert_bound_type(status, builder, scope, type_ref(lhs_type));
			if (!!status) {
				value_type = upsert_bound_type(status, builder, scope, lhs_type);
			}
		}
	}

	if (!!status) {
		/* generate the mutable stack-based variable for this var */
		llvm::Function *llvm_function = llvm_get_function(builder);

		// NOTE: we don't make this a gcroot until a little later on
		llvm::AllocaInst *llvm_alloca;
		if (value_type->is_managed_ptr(scope)) {
			llvm_alloca = llvm_call_gcroot(llvm_function, value_type, symbol);
		} else {
			llvm_alloca = llvm_create_entry_block_alloca(llvm_function, value_type, symbol);
		}

		if (init_var) {
			if (!init_var->type->get_type()->is_nil()) {
				debug_above(6, log(log_info, "creating a store instruction %s := %s",
							llvm_print(llvm_alloca).c_str(),
							llvm_print(init_var->get_llvm_value()).c_str()));

				// Should resolve_value be allowed?
				llvm::Value *llvm_init_value = init_var->get_llvm_value(); // ->resolve_value(builder);
				if (llvm_init_value->getName().size() == 0) {
					llvm_init_value->setName(string_format("%s.initializer", symbol.c_str()));
				}

				builder.CreateStore(
						llvm_maybe_pointer_cast(builder, llvm_init_value,
							value_type->get_llvm_specific_type()),
						llvm_alloca);
			} else {
				llvm::Constant *llvm_null_value = llvm::Constant::getNullValue(value_type->get_llvm_specific_type());
				builder.CreateStore(llvm_null_value, llvm_alloca);
			}
		} else if (dyncast<const types::type_maybe_t>(lhs_type)) {
			/* this can be null, let's initialize it as such */
			llvm::Constant *llvm_null_value = llvm::Constant::getNullValue(value_type->get_llvm_specific_type());
			builder.CreateStore(llvm_null_value, llvm_alloca);
		} else {
			user_error(status, obj.get_location(), "missing initializer");
		}

		if (!!status) {
			/* the reference_expr that looks at this llvm_value will need to
			 * know to use store/load semantics, not just pass-by-value */
			bound_var_t::ref var_decl_variable = bound_var_t::create(INTERNAL_LOC(), symbol,
					stack_var_type, llvm_alloca, make_type_id_code_id(obj.get_location(), obj.get_symbol()));

			/* memory management */
			call_addref_var(status, builder, scope, var_decl_variable,
					string_format("variable %s initialization", symbol.c_str()));
			if (!!status) {
				life->track_var(status, builder, scope, var_decl_variable, lf_block);

				if (!!status) {
					/* on our way out, stash the variable in the current scope */
					scope->put_bound_variable(status, var_decl_variable->name,
							var_decl_variable);

					if (!!status) {
						if (unboxed) {
							/* 'condition_value' refers to whether this was an unboxed maybe */
							bound_var_t::ref condition_value;

							assert(init_var != nullptr);
							assert(maybe_unbox);

							/* get the maybe type so that we can use it as a conditional */
							bound_type_t::ref condition_type = upsert_bound_type(status, builder, scope, lhs_type);
							llvm::Value *llvm_resolved_value = init_var->resolve_bound_var_value(builder);

							if (!!status) {
								/* we're unboxing a Maybe{any}, so let's return
								 * whether this was Empty or not... */
								return bound_var_t::create(INTERNAL_LOC(), symbol,
										condition_type, llvm_resolved_value,
										make_type_id_code_id(obj.get_location(), obj.get_symbol()));
							}
						} else {
							return var_decl_variable;
						}
					}
				}
			}
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref generate_module_variable(
		status_t &status,
		llvm::IRBuilder<> &builder,
		function_scope_t::ref scope,
		life_t::ref life,
		const ast::var_decl_t &obj,
		atom symbol,
		types::type_t::ref declared_type)
{
	/* 'init_var' is keeping track of the value we are assigning to our new
	 * variable (if any exists.) */
	bound_var_t::ref init_var;

	if (obj.initializer) {
		/* we have an initializer */
		init_var = obj.initializer->resolve_expression(status, builder,
				scope, life, false /*as_ref*/);
	}

	types::type_t::ref lhs_type = declared_type;
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
		/* 'type' is keeping track of what the variable's ending type will be */
		assert(lhs_type != nullptr);
		bound_type_t::ref bound_type = upsert_bound_type(status, builder, scope, lhs_type);

		/* generate the immutable global variable for this var */
		llvm::Constant *llvm_constant = nullptr;
		if (bound_type->get_llvm_specific_type()->isPointerTy()) {
			llvm_constant = llvm::Constant::getNullValue(bound_type->get_llvm_specific_type());
		} else {
			user_error(status, obj, "unsupported type for module variable %s. currently only managed types are supported",
					bound_type->str().c_str());
		}

		if (!!status) {
			llvm::GlobalVariable *llvm_global_variable = llvm_get_global(
					llvm_get_module(builder),
					symbol.str(),
					llvm_constant,
					false /*is_constant*/);

			if (init_var != nullptr) {
				debug_above(6, log(log_info, "creating a store instruction %s := %s",
							llvm_print(llvm_global_variable).c_str(),
							llvm_print(init_var->get_llvm_value()).c_str()));

				llvm::Value *llvm_init_value = init_var->resolve_bound_var_value(builder);

				if (llvm_init_value->getName().str().size() == 0) {
					llvm_init_value->setName(string_format("%s.initializer", symbol.c_str()));
				}

				builder.CreateStore(
						llvm_maybe_pointer_cast(builder, llvm_init_value,
							bound_type->get_llvm_specific_type()),
						llvm_global_variable);
			} else {
				user_error(status, obj, "module var " c_id("%s") " missing initializer",
						symbol.c_str());
			}

			if (!!status) {
				/* the reference_expr that looks at this llvm_value will need to
				 * know to use store/load semantics, not just pass-by-value, so
				 * we will mark the actual global variable as a type_ref */
				auto bound_global_type = upsert_bound_type(status, builder, scope, type_ref(bound_type->get_type()));
				if (!!status) {
					bound_var_t::ref var_decl_variable = bound_var_t::create(INTERNAL_LOC(), symbol,
							bound_global_type, llvm_global_variable, make_code_id(obj.token));

					/* on our way out, stash the variable in the current scope */
					scope->get_module_scope()->put_bound_variable(status, var_decl_variable->name,
							var_decl_variable);

					return var_decl_variable;
				}
			}
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref type_check_bound_var_decl(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		const ast::like_var_decl_t &obj,
		life_t::ref life,
		bool maybe_unbox)
{
	const atom symbol = obj.get_symbol();

	debug_above(4, log(log_info, "type_check_var_decl is looking for a type for variable " c_var("%s") " : %s",
				symbol.c_str(), obj.get_symbol().c_str()));

	assert(dyncast<module_scope_t>(scope) == nullptr);
	if (scope->has_bound_variable(symbol, rc_capture_level)) {
		user_error(status, obj.get_location(), "variables cannot be redeclared");
		return nullptr;
	}

	if (!!status) {
		assert(obj.get_type() != nullptr);

		/* 'declared_type' tells us the user-declared type on the left-hand side of
		 * the assignment. this is generally used to allow a variable to be more
		 * generalized than the specific right-hand side initial value might be. */
		types::type_t::ref declared_type = obj.get_type()->rebind(scope->get_type_variable_bindings());

		assert(dyncast<runnable_scope_t>(scope) != nullptr);

		return generate_stack_variable(status, builder, scope, life,
				obj, symbol, declared_type, maybe_unbox);
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref type_check_module_var_decl(
		status_t &status,
		llvm::IRBuilder<> &builder,
		function_scope_t::ref scope,
		const ast::var_decl_t &obj,
		life_t::ref life)
{
	const atom symbol = obj.token.text;

	debug_above(4, log(log_info, "type_check_module_var_decl is looking for a type for variable " c_var("%s") " : %s",
				symbol.c_str(), obj.str().c_str()));

	if (scope->get_module_scope()->has_bound_variable(symbol, rc_just_current_scope)) {
		user_error(status, obj, "module variables cannot be redeclared");
		return nullptr;
	}

	assert(obj.type != nullptr);

	if (!!status) {
		/* 'declared_type' tells us the user-declared type on the left-hand side of
		 * the assignment. this is generally used to allow a variable to be more
		 * generalized than the specific right-hand side initial value might be. */
		types::type_t::ref declared_type = obj.type->rebind(scope->get_type_variable_bindings());

		return generate_module_variable(status, builder, scope, life,
				obj, symbol, declared_type);
	}

	assert(!status);
	return nullptr;
}

atom::many get_param_list_decl_variable_names(ast::param_list_decl_t::ref obj) {
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
	for (size_t i = 0; i < args.size(); ++i) {
		named_args.push_back({names[i], args[i]});
	}
	return named_args;
}

status_t get_fully_bound_param_list_decl_variables(
		llvm::IRBuilder<> &builder,
		ast::param_list_decl_t &obj,
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
		types::type_t::ref type,
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
		const ast::function_decl_t &obj,
		scope_t::ref scope,
		types::type_t::ref &inbound_context,
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

bool type_is_unbound(types::type_t::ref type, types::type_t::map bindings) {
	return type->rebind(bindings)->ftv_count() > 0;
}

bool is_function_defn_generic(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		const ast::function_defn_t &obj)
{
	if (!!status) {
		if (obj.decl->param_list_decl) {
			/* check the parameters' genericity */
			auto &params = obj.decl->param_list_decl->params;
			for (auto &param : params) {
				if (!param->type) {
					debug_above(6, log(log_info, "found a missing parameter type on %s, defaulting it to an unnamed generic",
								param->str().c_str()));
					return true;
				}

				if (!!status) {
					if (type_is_unbound(param->type, scope->get_type_variable_bindings())) {
						debug_above(6, log(log_info, "found a generic parameter type on %s",
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
	}

	assert(!status);
	return false;
}

function_scope_t::ref make_param_list_scope(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ast::function_decl_t &obj,
		scope_t::ref &scope,
		life_t::ref life,
		bound_var_t::ref function_var,
		bound_type_t::named_pairs params)
{
	assert(!!status);
	assert(life->life_form == lf_function);

	if (!!status) {
		auto new_scope = scope->new_function_scope(
				string_format("function-%s", function_var->name.c_str()));

		assert(obj.param_list_decl->params.size() == params.size());

		llvm::Function *llvm_function = llvm::cast<llvm::Function>(function_var->get_llvm_value());
		llvm::Function::arg_iterator args = llvm_function->arg_begin();

		int i = 0;

		for (auto &param : params) {
			llvm::Value *llvm_param = &(*args++);
			if (llvm_param->getName().str().size() == 0) {
				llvm_param->setName(param.first.str());
			}

			assert(!param.second->is_ref());

			bool allow_reassignment = false;
			auto param_type = param.second->get_type();
			if (!param_type->is_ref() && !param_type->is_nil()) {
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
						llvm_function, param.second, param.first.str());

				// REVIEW: how to manage memory for named parameters? if we allow
				// changing their value then we have to enforce addref/release
				// semantics on them...
				debug_above(6, log(log_info, "creating a local alloca for parameter %s := %s",
							llvm_print(llvm_alloca).c_str(),
							llvm_print(llvm_param).c_str()));
				builder.CreateStore(llvm_param, llvm_alloca);	
				llvm_param_final = llvm_alloca;
			}

			auto bound_stack_var_type = upsert_bound_type(status, builder,
					scope, param_type);
			if (!!status) {
				auto param_var = bound_var_t::create(INTERNAL_LOC(), param.first, bound_stack_var_type,
						llvm_param_final, make_code_id(obj.param_list_decl->params[i++]->token));

				bound_type_t::ref return_type = get_function_return_type(scope, function_var->type);

				// TODO: an optimization here (to avoid the addref/release
				// overhead would be to check whether this symbol is on the LHS of
				// any assignment operations within this function AST's body. For
				// now, we'll be safe.
				call_addref_var(status, builder, scope, param_var, "function parameter lifetime");

				if (!!status) {
					life->track_var(status, builder, scope, param_var, lf_function);
				}

				if (!!status) {
					/* add the parameter argument to the current scope */
					new_scope->put_bound_variable(status, param.first, param_var);
				}
			}

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

void ast::expression_t::resolve_statement(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	/* expressions as statements just pass through to evaluating the expr */
	resolve_expression(status, builder, scope, life, false /*as_ref*/);
}

void ast::link_module_statement_t::resolve_statement(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
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
			return;
		}
	} else {
		user_error(status, *this, "can't find module %s", linked_module_name.c_str());
	}

	assert(!status);
	return;
}

bound_var_t::ref ast::link_function_statement_t::resolve_expression(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);

	/* FFI */
	module_scope_t::ref module_scope = dyncast<module_scope_t>(scope);
	assert(module_scope);

	if (!scope->has_bound_variable(function_name.text, rc_just_current_scope)) {
		types::type_t::ref inbound_context;
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

			assert(llvm_print(llvm_value->getType()) != llvm_print(llvm_func_type));

			/* get the full function type */
			types::type_function_t::ref function_sig = get_function_type(
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
					make_code_id(extern_function->token));
		}
	} else {
		auto bound_var = scope->get_bound_variable(status, shared_from_this(), function_name.text);
		user_error(status, *this, "name of " c_id("%s") " conflicts with %s",
			   function_name.text.c_str(), bound_var->str().c_str());
	}

	assert(!status);
	return nullptr;
}

void ast::link_name_t::resolve_statement(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	not_impl();
}

bound_var_t::ref ast::dot_expr_t::resolve_overrides(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		const ptr<const ast::item_t> &callsite,
		const bound_type_t::refs &args) const
{
	indent_logger indent(5, string_format(
				"dot_expr_t::resolve_overrides for %s",
				callsite->str().c_str()));

	/* check the left-hand side first, it should be a type_namespace */
	bound_var_t::ref lhs_var = lhs->resolve_expression(
			status, builder, scope, life, false /*as_ref*/);

	if (!!status) {
		if (auto bound_module = dyncast<const bound_module_t>(lhs_var)) {
			assert(bound_module->module_scope != nullptr);

			/* let's see if the associated module has a method that can handle this callsite */
			return get_callable(status, builder, bound_module->module_scope,
					rhs.text, callsite,
					bound_module->module_scope->get_outbound_context(),
					get_args_type(args));
		} else {
			// TODO: this looks like it needs to handle member functions
			user_error(status, *lhs, "left of a dot (\".\") must be a struct or module. this is not a struct or module. %s",
					lhs_var->str().c_str());
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::callsite_expr_t::resolve_expression(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);

	/* get the value of calling a function */
	bound_type_t::refs param_types;
	bound_var_t::refs arguments;

	if (auto symbol = dyncast<ast::reference_expr_t>(function_expr)) {
		if (symbol->token.text == "static_print") {
			if (params->expressions.size() == 1) {
				auto param = params->expressions[0];
				bound_var_t::ref param_var = param->resolve_expression(
						status, builder, scope, life, false /*as_ref*/);

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
			// TODO: consider changing the semantics of as_ref to be "allow_ref"
			// which would disallow the parameter from being a ref type, even if
			// it is naturally.
			bound_var_t::ref param_var = param->resolve_expression(
					status, builder, scope, life, false /*as_ref*/);

			if (!status) {
				break;
			}

			assert(!param_var->get_type()->is_ref());

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
					status, builder, scope, life, shared_from_this(),
					bound_type_t::refs_from_vars(arguments));

			if (!!status) {
				debug_above(5, log(log_info, "function chosen is %s", function->str().c_str()));

				return make_call_value(status, builder, get_location(), scope,
						life, function, arguments);
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

bound_var_t::ref ast::reference_expr_t::resolve_expression(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	/* we wouldn't be referencing a variable name here unless it was unique
	 * override resolution only happens on callsites, and we don't allow
	 * passing around unresolved overload references */
	bound_var_t::ref var = scope->get_bound_variable(status, shared_from_this(),
			token.text);

	/* get_bound_variable can return nullptr without an user_error */
	if (var != nullptr) {
		assert(!!status);

		if (!as_ref) {
			return var->resolve_bound_value(status, builder, scope);
		} else {
			return var;
		}
	}

	user_error(status, *this, "undefined symbol " c_id("%s"), token.text.c_str());
	return nullptr;
}

bound_var_t::ref ast::reference_expr_t::resolve_as_condition(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		local_scope_t::ref *new_scope) const
{
	bound_var_t::ref var = scope->get_bound_variable(status, shared_from_this(), token.text);

	if (!var) {
		user_error(status, *this, "undefined symbol " c_id("%s"), token.text.c_str());
	}

	bound_var_t::ref test_var = var;
	bool was_ref = var->type->is_ref();
	if (was_ref) {
		test_var = var->resolve_bound_value(status, builder, scope);
		assert(!test_var->type->is_ref());
	}

	if (!!status) {
		if (auto maybe_type = dyncast<const types::type_maybe_t>(test_var->type->get_type())) {
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
					was_ref ? type_ref(maybe_type->just) : maybe_type->just);

			if (!!status) {
				// TODO: decide whether this variable can be made into a ref
				// type if "was_ref" is true, to enable reassignment. The
				// downfall of this is that even if this is allowed, then a null
				// value can never be assigned to this value. This could
				// generally be considered good in the abstract, however in
				// practice it's common for users to want to assign variables a
				// nil value as a way of signaling loop exits and the like.

				/* because we're evaluating this maybe value in the context of a
				 * condition (super simplified at this point), let's redeclare it
				 * without its maybe, since we know it will be valid if the
				 * condition passes */
				bound_var_t::ref var_decl_variable =
					bound_var_t::create(INTERNAL_LOC(), token.text, bound_type,
							var->get_llvm_value(), make_code_id(token));

				/* on our way out, stash the variable in the current scope */
				scope->put_bound_variable(status, var_decl_variable->name,
						var_decl_variable);

				/* get the maybe type so that we can use it as a conditional */
				bound_type_t::ref condition_type = upsert_bound_type(status, builder, scope, maybe_type);
				if (!!status) {
					return bound_var_t::create(INTERNAL_LOC(), token.text,
							condition_type, test_var->get_llvm_value(),
						   	make_code_id(token));
				}
			}

			assert(!status);
			return nullptr;
		} else {
			/* this is not a maybe, so let's just move along */
			return test_var;
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::array_index_expr_t::resolve_expression(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);

	/* this expression looks like this
	 *
	 *   lhs[index]
	 *
	 */

	if (!!status) {
		bound_var_t::ref lhs_val = lhs->resolve_expression(status, builder,
				scope, life, false /*as_ref*/);

		if (!!status) {
			bound_var_t::ref index_val = index->resolve_expression(status, builder,
					scope, life, false /*as_ref*/);

			if (!!status) {
				/* get or instantiate a function we can call on these arguments */
				return call_program_function(status, builder, scope, life,
						"__getitem__", shared_from_this(), {lhs_val, index_val});
			}
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::array_literal_expr_t::resolve_expression(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);
	user_error(status, *this, "not impl");
	return nullptr;
}

bound_var_t::ref type_check_binary_operator(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		ptr<const ast::expression_t> lhs,
		ptr<const ast::expression_t> rhs,
		ast::item_t::ref obj,
		atom function_name)
{
	if (!!status) {
		assert(function_name.size() != 0);

		bound_var_t::ref lhs_var, rhs_var;
		lhs_var = lhs->resolve_expression(status, builder, scope, life,
				false /*as_ref*/);
		if (!!status) {
			assert(!lhs_var->type->is_ref());

			if (!!status) {
				rhs_var = rhs->resolve_expression(status, builder, scope, life,
						false /*as_ref*/);
				assert(!rhs_var->type->is_ref());

				if (!!status) {
					/* get or instantiate a function we can call on these arguments */
					return call_program_function(
							status, builder, scope, life, function_name,
							obj, {lhs_var, rhs_var});
				}
			}
		}
	}
	assert(!status);
	return nullptr;
}

bound_var_t::ref type_check_binary_equality(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		ptr<const ast::expression_t> lhs,
		ptr<const ast::expression_t> rhs,
		ast::item_t::ref obj,
		bool negated)
{
	if (!!status) {
		bound_var_t::ref lhs_var, rhs_var;
		lhs_var = lhs->resolve_expression(status, builder, scope, life,
				false /*as_ref*/);
		if (!!status) {
			rhs_var = rhs->resolve_expression(status, builder, scope, life,
					false /*as_ref*/);

			if (!!status) {
				unification_t unification_rtl = unify(
					lhs_var->type->get_type(),
					rhs_var->type->get_type(),
					scope->get_typename_env());

				if (!unification_rtl.result) {
					unification_t unification_ltr = unify(
						rhs_var->type->get_type(),
						lhs_var->type->get_type(),
						scope->get_typename_env());

					if (!unification_ltr.result) {
						user_error(status, obj->get_location(),
								   "these two expressions cannot possibly refer to the same value");
						user_message(log_info, status, obj->get_location(), "%s is of type %s, %s is of type %s",
									 lhs->str().c_str(),
									 lhs_var->type->get_type()->str().c_str(),
									 rhs->str().c_str(),
									 rhs_var->type->get_type()->str().c_str());
						return nullptr;
					}
				}

				if (lhs_var->type->is_managed_ptr(scope)) {
					assert(rhs_var->type->get_type()->is_nil() || rhs_var->type->is_managed_ptr(scope));
					auto program_scope = scope->get_program_scope();
					auto llvm_var_ref_type = program_scope->get_bound_type({"__var_ref"})->get_llvm_type();
					llvm::Value *llvm_value = negated
						? builder.CreateICmpNE(
							llvm_maybe_pointer_cast(builder, lhs_var->resolve_bound_var_value(builder), llvm_var_ref_type),
							llvm_maybe_pointer_cast(builder, rhs_var->resolve_bound_var_value(builder), llvm_var_ref_type))
						: builder.CreateICmpEQ(
							llvm_maybe_pointer_cast(builder, lhs_var->resolve_bound_var_value(builder), llvm_var_ref_type),
							llvm_maybe_pointer_cast(builder, rhs_var->resolve_bound_var_value(builder), llvm_var_ref_type));

					return bound_var_t::create(
						INTERNAL_LOC(),
						{"equality.cond"},
						program_scope->get_bound_type(BOOL_TYPE),
						llvm_value,
						make_code_id(obj->token));
				} else {
					// TODO: consider enabling comparison of raw pointers?
					user_error(status, obj->get_location(),
							   "comparing identities of native values is not yet implemented");
				}
			}
		}
	}
	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::eq_expr_t::resolve_expression(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);

	atom function_name;
	switch (token.tk) {
	case tk_equal:
		function_name = "__eq__";
		break;
	case tk_inequal:
		function_name = "__ineq__";
		break;
	case tk_is:
		return type_check_binary_equality(status, builder, scope, life, lhs, rhs,
										  shared_from_this(), negated);
	default:
		return null_impl();
	}

	return type_check_binary_operator(status, builder, scope, life, lhs, rhs,
			shared_from_this(), function_name);
}

bound_var_t::ref ast::tuple_expr_t::resolve_expression(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);

	/* let's get the actual values in our tuple. */
	bound_var_t::refs vars;
	vars.reserve(values.size());

	for (auto &value: values) {
		bound_var_t::ref var = value->resolve_expression(status, builder,
				scope, life, false /*as_ref*/);
		if (!!status) {
			vars.push_back(var);
		}
	}

	if (!!status) {
		bound_type_t::refs args = get_bound_types(vars);

		/* let's get the type for this tuple wrapped as an object */
		types::type_t::ref tuple_type = get_tuple_type(args);

		/* now, let's see if we already have a ctor for this tuple type, if not
		 * we'll need to create a data ctor for this unnamed tuple type */
		auto program_scope = scope->get_program_scope();

		std::pair<bound_var_t::ref, bound_type_t::ref> tuple = instantiate_tuple_ctor(
				status, builder, scope,
				scope->get_inbound_context(), args,
				make_iid(tuple_type->repr()), shared_from_this());

		if (!!status) {
			assert(get_function_return_type(scope, tuple.first->type)->get_type()->repr() == type_ptr(type_managed(tuple_type))->repr());

			/* now, let's call our unnamed tuple ctor and return that value */
			return create_callsite(status, builder, scope, life,
					tuple.first, tuple_type->repr(),
					token.location, vars);
		}
	}

	assert(!status);
	return nullptr;
}

llvm::Value *get_raw_condition_value(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		ast::item_t::ref condition,
		bound_var_t::ref condition_value)
{
	if (condition_value->is_int()) {
		return condition_value->resolve_bound_var_value(builder);
	} else if (condition_value->is_pointer()) {
		return condition_value->resolve_bound_var_value(builder);
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
		life_t::ref life,
		ast::item_t::ref condition,
		bound_var_t::ref condition_value)
{
	condition_value = condition_value->resolve_bound_value(status, builder, scope);
	if (!!status) {
		assert(life->life_form == lf_statement);

		llvm::Value *llvm_condition_value = nullptr;
		// TODO: check whether we are checking a raw value or not

		debug_above(2, log(log_info,
					"attempting to resolve a " c_var("%s") " override if condition %s, ",
					BOOL_TYPE,
					condition->str().c_str()));

		/* we only ever get in here if we are definitely non-null, so we can discard
		 * maybe type specifiers */
		types::type_t::ref condition_type;
		if (auto maybe = dyncast<const types::type_maybe_t>(condition_value->type->get_type())) {
			condition_type = maybe->just;
		} else {
			condition_type = condition_value->type->get_type();
		}

		var_t::refs fns;
		auto bool_fn = maybe_get_callable(status, builder, scope, BOOL_TYPE,
				condition->get_location(), scope->get_outbound_context(),
				type_args({condition_type}), fns);

		if (!!status) {
			if (bool_fn != nullptr) {
				/* we've found a bool function that will take our condition as input */
				assert(bool_fn != nullptr);

				if (get_function_return_type(bool_fn->type->get_type())->get_signature() == "__bool__") {
					debug_above(7, log(log_info, "generating a call to " c_var("bool") "(%s) for if condition evaluation (type %s)",
								condition->str().c_str(), bool_fn->type->str().c_str()));

					/* let's call this bool function */
					llvm_condition_value = llvm_create_call_inst(
							status, builder, condition->get_location(), bool_fn,
							{condition_value->resolve_bound_var_value(builder)});

					if (!!status) {
						/* NB: no need to track this value in a life because it's
						 * returning an integer type */
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
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::ternary_expr_t::resolve_expression(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);

	/* if scope allows us to set up new variables inside if conditions */
	local_scope_t::ref if_scope;

	bound_type_t::ref type_constraint;

	assert(condition != nullptr);

	bound_var_t::ref condition_value;

	/* evaluate the condition for branching */
	if (auto var_decl = dyncast<const ast::var_decl_t>(condition)) {
		/* our user is attempting an assignment inside of an if statement, let's
		 * grant them a favor, and automatically unbox the Maybe type if it
		 * exists. */
		condition_value = var_decl->resolve_as_condition(
				status, builder, scope, life, &if_scope);
	} else if (auto ref_expr = dyncast<const ast::reference_expr_t>(condition)) {
		condition_value = ref_expr->resolve_as_condition(
				status, builder, scope, life, &if_scope);
	} else {
		condition_value = condition->resolve_expression(
				status, builder, scope, life, false /*as_ref*/);
	}

	if (!!status) {
		/* if the condition value is a maybe type, then we'll need multiple
		 * anded conditions to be true in order to actually fall into the then
		 * block, let's figure out those conditions */
		llvm::Value *llvm_raw_condition_value = get_raw_condition_value(status,
				builder, scope, condition, condition_value);

		if (!!status) {
			assert(llvm_raw_condition_value != nullptr);

			llvm::Function *llvm_function_current = llvm_get_function(builder);

			/* generate some new blocks */
			llvm::BasicBlock *then_bb = llvm::BasicBlock::Create(
					builder.getContext(), "ternary.truthy", llvm_function_current);

			/* we've got an else block, so let's create an "else" basic block. */
			llvm::BasicBlock *else_bb = llvm::BasicBlock::Create(
					builder.getContext(), "ternary.falsey", llvm_function_current);

			/* put the merge block after the else block */
			llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(
					builder.getContext(), "ternary.phi", llvm_function_current);

			/* create the actual branch instruction */
			llvm_create_if_branch(status, builder, scope, 0,
					nullptr, llvm_raw_condition_value, then_bb, else_bb);

			if (!!status) {
				/* calculate the false path's value in the else block */
				builder.SetInsertPoint(else_bb);
				bound_var_t::ref false_path_value = when_false->resolve_expression(
						status, builder, scope, life, false /*as_ref*/);

				/* after calculation, the code should jump to the phi node's basic block */
				builder.CreateBr(merge_bb);

				if (!!status) {
					/* let's generate code for the "true-path" block */
					builder.SetInsertPoint(then_bb);
					llvm::Value *llvm_bool_overload_value = maybe_get_bool_overload_value(status,
							builder, scope, life, condition, condition_value);

					if (!!status) {
						llvm::BasicBlock *truth_path_bb = then_bb;

						if (llvm_bool_overload_value != nullptr) {
							/* we've got a second condition to check, let's do it */
							auto deep_then_bb = llvm::BasicBlock::Create(
									builder.getContext(), "ternary.truthy.__bool__", llvm_function_current);

							llvm_create_if_branch(status, builder, scope, 0,
									nullptr, llvm_bool_overload_value,
									deep_then_bb, else_bb ? else_bb : merge_bb);
							builder.SetInsertPoint(deep_then_bb);

							/* make sure the phi node knows to consider this deeper
							 * block as the source of one of its possible inbound
							 * values */
							truth_path_bb = deep_then_bb;
						}

						if (!!status) {
							/* get the bound_var for the truthy path */
							bound_var_t::ref true_path_value = when_true->resolve_expression(
									status, builder, if_scope ? if_scope : scope, life, false /*as_ref*/);

							if (!!status) {
								bound_type_t::ref ternary_type;
								if (true_path_value->type->get_llvm_specific_type() != false_path_value->type->get_llvm_specific_type()) {
									/* the when_true and when_false values have different
									 * types, let's create a sum type to represent this */
									auto ternary_sum_type = type_sum_safe(status, {
											true_path_value->type->get_type(),
											false_path_value->type->get_type()});

									if (!!status) {
										ternary_type = upsert_bound_type(status,
												builder, scope, ternary_sum_type);
									}
								} else {
									ternary_type = true_path_value->type;
								}

								if (!!status) {
									builder.CreateBr(merge_bb);
									builder.SetInsertPoint(merge_bb);

									llvm::PHINode *llvm_phi_node = llvm::PHINode::Create(
											ternary_type->get_llvm_specific_type(),
											2, "ternary.phi.node", merge_bb);

									llvm_phi_node->addIncoming(llvm_maybe_pointer_cast(
												builder, true_path_value->resolve_bound_var_value(builder), ternary_type),
											truth_path_bb);

									llvm_phi_node->addIncoming(llvm_maybe_pointer_cast(
												builder, false_path_value->resolve_bound_var_value(builder), ternary_type),
											else_bb);

									return bound_var_t::create(
											INTERNAL_LOC(),
											{"ternary.value"},
											ternary_type,
											llvm_phi_node,
											make_code_id(this->token));
								}
							}
						}
					}
				}
			}
		}
	}

	assert(!status);
    return nullptr;
}

bound_var_t::ref ast::or_expr_t::resolve_expression(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);
	// TODO: implement short-circuiting
	return null_impl();
}

bound_var_t::ref ast::and_expr_t::resolve_expression(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);
	// TODO: implement short-circuiting
	return null_impl();
}

types::type_struct_t::ref get_struct_type_from_ptr(
		status_t &status,
		scope_t::ref scope,
		ast::item_t::ref node,
		types::type_t::ref type)
{
	auto original_type = type;
	auto expanded_type = eval(type, scope->get_typename_env());
	if (expanded_type != nullptr) {
		type = expanded_type;
	}

	if (auto ptr_type = dyncast<const types::type_ptr_t>(type)) {
		if (auto managed_type = dyncast<const types::type_managed_t>(ptr_type->element_type)) {
			if (auto struct_type = dyncast<const types::type_struct_t>(managed_type->element_type)) {
				return struct_type;
			}
		} else {
			if (auto struct_type = dyncast<const types::type_struct_t>(ptr_type->element_type)) {
				return struct_type;
			}
		}
	}

	user_error(status, node->get_location(),
		   	"could not find structured type within %s", original_type->str().c_str());

	assert(!status);
	return nullptr;
}

bound_var_t::ref extract_member_variable(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		ast::item_t::ref node,
		bound_var_t::ref bound_var,
		atom member_name,
		bool as_ref)
{
	if (!!status) {
		types::type_struct_t::ref struct_type = get_struct_type_from_ptr(
				status, scope, node, bound_var->get_type());

		if (!status) {
			return nullptr;
		}

		bound_type_t::ref bound_obj_type = bound_var->type;

		auto expanded_type = eval(bound_var->type->get_type(), scope->get_typename_env());
		if (expanded_type != nullptr) {
			bound_obj_type = upsert_bound_type(status, builder, scope, expanded_type);
		} else {
			/* this may be an unnamed tuple, so in that case it doesn't need
			 * expanding */
		}

		debug_above(5, log(log_info, "looking for member " c_id("%s") " in %s", member_name.c_str(),
					bound_obj_type->str().c_str()));

		auto member_index = struct_type->name_index;
		auto member_index_iter = member_index.find(member_name);

		for (auto member_index_pair : member_index) {
			debug_above(5, log(log_info, "%s: %d", member_index_pair.first.c_str(),
						member_index_pair.second));
		}

		if (member_index_iter != member_index.end()) {
			auto index = member_index_iter->second;
			debug_above(5, log(log_info, "found member " c_id("%s") " of type %s at index %d",
						member_name.c_str(),
						struct_type->str().c_str(),
						index));

			debug_above(5, log(log_info, "looking at bound_var %s : %s",
						bound_var->str().c_str(),
						llvm_print(bound_var->type->get_llvm_type()).c_str()));

			/* get an GEP-able version of the object */
			llvm::Value *llvm_var_value = bound_var->resolve_bound_var_value(builder);

			llvm_var_value = llvm_maybe_pointer_cast(builder, llvm_var_value,
					bound_obj_type->get_llvm_specific_type());

			/* the following code is heavily coupled to the physical layout of
			 * managed vs. native structures */

			/* GEP and load the member value from the structure */
			llvm::Value *llvm_gep = llvm_make_gep(builder,
					llvm_var_value, index,
					types::is_managed_ptr(bound_var->get_type(),
						scope->get_typename_env()) /* managed */);
			if (llvm_gep->getName().str().size() == 0) {
				llvm_gep->setName(string_format("address_of.%s", member_name.c_str()));
			}

			llvm::Value *llvm_item = as_ref ? llvm_gep : builder.CreateLoad(llvm_gep);

			/* add a helpful descriptive name to this local value */
			auto value_name = string_format(".%s", member_name.c_str());
			llvm_item->setName(value_name);

			/* get the type of the dimension being referenced */
			bound_type_t::ref member_type;
			if (as_ref) {
				member_type = upsert_bound_type(status, builder, scope, 
						type_ref(struct_type->dimensions[index]));
			} else {
				member_type = scope->get_bound_type(struct_type->dimensions[index]->get_signature());
			}

			if (!!status) {
				return bound_var_t::create(
						INTERNAL_LOC(), value_name,
						member_type, llvm_item, make_iid(member_name));
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
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::dot_expr_t::resolve_expression(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	debug_above(6, log("resolving dot_expr %s", str().c_str()));
	bound_var_t::ref lhs_val = lhs->resolve_expression(status,
			builder, scope, life, false /*as_ref*/);

	if (!!status) {
		types::type_t::ref member_type;

		if (!!status) {
			if (lhs_val->type->is_module()) {
				std::string qualified_id = string_format("%s%s%s",
						lhs_val->name.c_str(),
						SCOPE_SEP,
						rhs.text.c_str());
				debug_above(5,
						log("attempt to find global id %s",
							qualified_id.c_str()));
				auto var = scope->get_bound_variable(status, shared_from_this(),
						qualified_id);
				if (var != nullptr) {
					return var;
				} else {
					user_error(status, get_location(),
							"could not find symbol %s",
							qualified_id.c_str());
				}
			} else {
				return extract_member_variable(status, builder, scope, life,
						shared_from_this(), lhs_val, rhs.text, as_ref);
			}
		}
	}

    assert(!status);
    return nullptr;
}

bound_var_t::ref ast::ineq_expr_t::resolve_expression(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);

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

	return type_check_binary_operator(status, builder, scope, life, lhs, rhs,
			shared_from_this(), function_name);
}

bound_var_t::ref ast::plus_expr_t::resolve_expression(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);

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

	return type_check_binary_operator(status, builder, scope, life, lhs, rhs,
			shared_from_this(), function_name);
}

bound_var_t::ref call_typeid(
		status_t &status,
		scope_t::ref scope,
		life_t::ref life,
		ast::item_t::ref callsite,
		identifier::ref id,
	   	llvm::IRBuilder<> &builder,
		bound_var_t::ref resolved_value)
{
	resolved_value = resolved_value->resolve_bound_value(status, builder, scope);
	if (!!status) {
		debug_above(4, log(log_info, "getting typeid of %s",
					resolved_value->type->str().c_str()));
		auto program_scope = scope->get_program_scope();

		auto llvm_type = resolved_value->type->get_llvm_specific_type();
		auto llvm_obj_type = program_scope->get_bound_type({"__var"})->get_llvm_type();
		bool is_obj = false;

		if (llvm_type->isPointerTy()) {
			if (auto llvm_struct = llvm::dyn_cast<llvm::StructType>(llvm_type->getPointerElementType())) {
				is_obj = (
						llvm_struct == llvm_obj_type ||
						llvm_struct->getStructElementType(0) == llvm_obj_type);
			}
		}

		auto name = string_format("typeid(%s)", resolved_value->str().c_str());

		if (is_obj) {
			auto get_typeid_function = program_scope->get_bound_variable(status,
					callsite, "__get_var_type_id");
			if (!!status) {
				return create_callsite(
						status,
						builder,
						scope,
						life,
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
					llvm_create_int32(builder, resolved_value->type->get_type()->get_signature().iatom),
					id);
		}
	}

	assert(!status);
	return nullptr;
}


bound_var_t::ref ast::typeid_expr_t::resolve_expression(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);

	auto resolved_value = expr->resolve_expression(status,
			builder,
			scope,
			life,
			false /*as_ref*/);

	if (!!status) {
		return call_typeid(status, scope, life, shared_from_this(),
				make_code_id(token), builder, resolved_value);
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::sizeof_expr_t::resolve_expression(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);

	/* calculate the size of the object being referenced assume native types */
	bound_type_t::ref bound_type = upsert_bound_type(status, builder, scope, type);
	bound_type_t::ref size_type = scope->get_program_scope()->get_bound_type({INT_TYPE});
	if (!!status) {
		llvm::Value *llvm_size = llvm_sizeof_type(builder,
				llvm_deref_type(bound_type->get_llvm_specific_type()));

		return bound_var_t::create(
				INTERNAL_LOC(), type->str(), size_type, llvm_size,
				make_iid("sizeof"));
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::function_defn_t::resolve_expression(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);

	return resolve_function(status, builder, scope, life, nullptr, nullptr);
}

void ast::function_defn_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	resolve_function(status, builder, scope, life, new_scope, returns);
}

bound_var_t::ref ast::function_defn_t::resolve_function(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	// TODO: handle first-class functions by handling life for functions, and closures.
	llvm::IRBuilderBase::InsertPointGuard ipg(builder);

	/* lifetimes have extents at function boundaries */
	auto life = make_ptr<life_t>(status, lf_function);

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
	types::type_t::ref inbound_context;
	bound_type_t::named_pairs args;
	bound_type_t::ref return_type;
	type_check_fully_bound_function_decl(status, builder, *decl, scope, inbound_context, args, return_type);

	if (!!status) {
		return instantiate_with_args_and_return_type(status, builder, scope, life,
				new_scope, inbound_context, args, return_type);
	} else {
		user_error(status, *this, "unable to declare function %s due to related errors",
				token.str().c_str());
	}

	assert(!status);
	return nullptr;
}

#define USER_MAIN_FN "user/main"

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

bound_var_t::ref ast::function_defn_t::instantiate_with_args_and_return_type(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		local_scope_t::ref *new_scope,
		types::type_t::ref inbound_context,
		bound_type_t::named_pairs args,
		bound_type_t::ref return_type) const
{
	std::string function_name = switch_std_main(token.text);
	indent_logger indent(5, string_format(
				"instantiating function " c_id("%s"),
				function_name.c_str()));

	llvm::IRBuilderBase::InsertPointGuard ipg(builder);
	assert(!!status);
	assert(life->life_form == lf_function);
	assert(life->values.size() == 0);

	assert(scope->get_llvm_module() != nullptr);

	auto function_type = get_function_type(inbound_context, args, return_type);
	bound_type_t::ref bound_function_type = upsert_bound_type(status,
			builder, scope, function_type);

	if (!!status) {
		assert(bound_function_type->get_llvm_type() != nullptr);

		llvm::Type *llvm_type = bound_function_type->get_llvm_specific_type();
		if (llvm_type->isPointerTy()) {
			llvm_type = llvm_type->getPointerElementType();
		}
		debug_above(5, log(log_info, "creating function %s with LLVM type %s",
				function_name.c_str(),
				llvm_print(llvm_type).c_str()));
		assert(llvm_type->isFunctionTy());

		llvm::Function *llvm_function = llvm::Function::Create(
				(llvm::FunctionType *)llvm_type,
				llvm::Function::ExternalLinkage, function_name,
				scope->get_llvm_module());

		llvm_function->setGC(GC_STRATEGY);
		llvm_function->setDoesNotThrow();

		llvm::BasicBlock *llvm_block = llvm::BasicBlock::Create(
				builder.getContext(), "entry", llvm_function);
		builder.SetInsertPoint(llvm_block);

		/* set up the mapping to this function for use in recursion */
		bound_var_t::ref function_var = bound_var_t::create(
				INTERNAL_LOC(), token.text, bound_function_type, llvm_function,
				make_code_id(token));

		/* we should be able to check its block as a callsite. note that this
		 * code will also run for generics but only after the
		 * sbk_generic_substitution mechanism has run its course. */
		auto params_scope = make_param_list_scope(status, builder, *decl, scope,
				life, function_var, args);

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

			block->resolve_statement(status, builder, params_scope, life,
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

						life->release_vars(status, builder, scope, lf_function);
						builder.CreateRetVoid();
						return function_var;
					} else {
						/* no breaks here, we don't know what to return */
						user_error(status, *this, "not all control paths return a value");
					}
				}

				llvm_verify_function(status, llvm_function);
			} else {
				user_error(status, get_location(), "while checking %s", function_var->str().c_str());
			}
		}
	}

    assert(!status);
    return nullptr;
}

void type_check_module_links(
		status_t &status,
		compiler_t &compiler,
		llvm::IRBuilder<> &builder,
		const ast::module_t &obj,
		scope_t::ref program_scope)
{
	if (!!status) {
		indent_logger indent(3, string_format("resolving links in " c_module("%s"),
					obj.module_key.c_str()));

		/* get module level scope variable */
		module_scope_t::ref scope = compiler.get_module_scope(obj.module_key);

		for (auto &link : obj.linked_modules) {
			link->resolve_statement(status, builder, scope, nullptr, nullptr,
					nullptr);
		}

		if (!!status) {
			for (auto &link : obj.linked_functions) {
				bound_var_t::ref link_value = link->resolve_expression(
						status, builder, scope, nullptr, false /*as_ref*/);

				if (!!status) {
					if (link->function_name.text.size() != 0) {
						scope->put_bound_variable(status, link->function_name.text, link_value);
					} else {
						user_error(status, *link, "module level link definitions need names");
					}
				}
			}
		}
	}
}

void type_check_module_vars(
		status_t &status,
        compiler_t &compiler,
		llvm::IRBuilder<> &builder,
		const ast::module_t &obj,
		scope_t::ref program_scope)
{
	if (!!status) {
		indent_logger indent(3, string_format("resolving module vars in " c_module("%s"),
					obj.module_key.c_str()));


		/* get module level scope variable */
		module_scope_t::ref module_scope = compiler.get_module_scope(obj.module_key);
		auto scope = module_scope->new_function_scope("__init_module_vars");
		assert(llvm_get_function(builder) != nullptr);

		for (auto &var_decl : obj.var_decls) {
			debug_above(6, log("instantiating module var " c_id("%s"),
						module_scope->make_fqn(var_decl->token.text).c_str()));
			/* track memory consumed during global construction */
			auto life = make_ptr<life_t>(status, lf_statement);

			/* the idea here is to put this variable into module scope,
			 * available globally, but to initialize it in the
			 * __init_module_vars function */
			type_check_module_var_decl(status, builder, scope, *var_decl, life);

			/* clean up any memory consumed during global construction */
			life->release_vars(status, builder, scope, lf_statement);
		}
	}
}

void type_check_module_types(
		status_t &status,
        compiler_t &compiler,
        llvm::IRBuilder<> &builder,
        const ast::module_t &obj,
        scope_t::ref program_scope)
{
	if (!!status) {
		indent_logger indent(2, string_format("type-checking types in module " c_module("%s"),
					obj.module_key.str().c_str()));

		/* get module level scope types */
		module_scope_t::ref module_scope = compiler.get_module_scope(obj.module_key);

		auto unchecked_types_ordered = module_scope->get_unchecked_types_ordered();
		for (auto unchecked_type : unchecked_types_ordered) {
			auto node = unchecked_type->node;
			if (!module_scope->has_checked(node)) {
				assert(!dyncast<const ast::function_defn_t>(node));

				/* prevent recurring checks */
				debug_above(5, log(log_info, "checking module level type %s", node->token.str().c_str()));

				/* these next lines create type definitions, regardless of
				 * their genericity.  type expressions will be added as
				 * environment variables in the type system.  this step is
				 * MUTATING the type environment of the module, and the
				 * program. */
				if (auto type_def = dyncast<const ast::type_def_t>(node)) {
					status_t local_status;
					type_def->resolve_statement(local_status, builder,
							module_scope, nullptr, nullptr, nullptr);

					/* take note of whether this failed or not */
					status |= local_status;
				} else if (auto tag = dyncast<const ast::tag_t>(node)) {
					status_t local_status;
					tag->resolve_statement(local_status, builder, module_scope,
							nullptr, nullptr, nullptr);

					/* take note of whether this failed or not */
					status |= local_status;
				} else {
					panic("unhandled unchecked type node at module scope");
				}
			} else {
				debug_above(3, log(log_info, "skipping %s because it's already been checked", node->token.str().c_str()));
			}
		}
	}
}

void type_check_program_variables(
		status_t &status,
        compiler_t &compiler,
        llvm::IRBuilder<> &builder,
        program_scope_t::ref program_scope)
{
	indent_logger indent(2, string_format("resolving variables in program"));

	auto unchecked_vars_ordered = program_scope->get_unchecked_vars_ordered();
    for (auto unchecked_var : unchecked_vars_ordered) {
		debug_above(5, log(log_info, "checking whether to check %s",
					unchecked_var->str().c_str()));

		auto node = unchecked_var->node;
		if (!unchecked_var->module_scope->has_checked(node)) {
			/* prevent recurring checks */
			debug_above(4, log(log_info, "checking module level variable %s",
					   	node->token.str().c_str()));
			if (auto function_defn = dyncast<const ast::function_defn_t>(node)) {
				// TODO: decide whether we need treatment here
				status_t local_status;
				if (is_function_defn_generic(local_status, builder,
							unchecked_var->module_scope, *function_defn))
			   	{
					/* this is a generic function, or we've already checked
					 * it so let's skip checking it */
					status |= local_status;
					continue;
				}
			}

			if (auto function_defn = dyncast<const ast::function_defn_t>(node)) {
				if (getenv("MAIN_ONLY") != nullptr && node->token.text != "__main__") {
					debug_above(8, log(log_info, "skipping %s because it's not '__main__'",
								node->str().c_str()));
					continue;
				}
			}

			status_t local_status;
			if (auto stmt = dyncast<const ast::statement_t>(node)) {
				stmt->resolve_statement(
						local_status, builder, unchecked_var->module_scope,
						nullptr, nullptr, nullptr);
				status |= local_status;
			} else if (auto data_ctor = dyncast<const ast::type_product_t>(node)) {
				/* ignore until instantiation at a callsite */
			} else {
				panic("unhandled unchecked node at module scope");
			}
		} else {
			debug_above(3, log(log_info, "skipping %s because it's already been checked", node->token.str().c_str()));
		}
    }
}

void type_check_all_module_var_slots(
		status_t &status,
		compiler_t &compiler,
		llvm::IRBuilder<> &builder,
		const ast::program_t &obj,
		program_scope_t::ref program_scope)
{
	/* build the global __init_module_vars function */
	if (!!status) {
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		bound_var_t::ref init_module_vars = llvm_start_function(status,
				builder, 
				program_scope,
				static_cast<const ast::item_t&>(obj).shared_from_this(),
				type_module(type_variable(INTERNAL_LOC())),
				{},
				program_scope->get_bound_type({"void"}),
				"__init_module_vars");

		if (!!status) {
			program_scope->put_bound_variable(status, "__init_module_vars", init_module_vars);

			if (!!status) {
				for (auto &module : obj.modules) {
					type_check_module_vars(status, compiler, builder, *module, program_scope);
				}

				builder.CreateRetVoid();
			}
		}
	}
}

void type_check_program(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ast::program_t &obj,
		compiler_t &compiler)
{
	indent_logger indent(2, string_format(
				"type-checking program %s",
				compiler.get_program_name().c_str()));

	ptr<program_scope_t> program_scope = compiler.get_program_scope();
	debug_above(11, log(log_info, "type_check_program program scope:\n%s", program_scope->str().c_str()));

	/* pass to resolve all module-level types */
	for (auto &module : obj.modules) {
		type_check_module_types(status, compiler, builder, *module, program_scope);
		if (!status) {
			break;
		}
	}

	if (!!status) {
		/* pass to resolve all module-level links */
		for (auto &module : obj.modules) {
			type_check_module_links(status, compiler, builder, *module, program_scope);
		}

		if (!!status) {
			/* pass to resolve all module-level vars */
			type_check_all_module_var_slots(status, compiler, builder, obj, program_scope);

			if (!!status) {
				assert(compiler.main_module != nullptr);

				/* pass to resolve all main module-level variables.  technically we only
				 * need to check the primary module, since that is the one that is expected
				 * to have the entry point ... at least for now... */
				type_check_program_variables(status, compiler, builder, program_scope);
			}
		}
	}
}

void ast::tag_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        local_scope_t::ref *new_scope,
		bool * /*returns*/) const
{
	atom tag_name = token.text;
	atom fqn_tag_name = scope->make_fqn(tag_name.str());
	auto qualified_id = make_iid_impl(fqn_tag_name, token.location);

	auto tag_type = type_id(qualified_id);

	/* it's a nullary enumeration or "tag", let's create a global value to
	 * represent this tag. */

	if (!!status) {
		/* start by making a type for the tag */
		bound_type_t::ref bound_tag_type = bound_type_t::create(
				tag_type,
				token.location,
				/* all tags use the var_t* type */
				scope->get_program_scope()->get_bound_type({"__var_ref"})->get_llvm_type());

		scope->put_typename(status, fqn_tag_name, type_ptr(type_managed(type_struct({}, {}))));
		if (!!status) {
			scope->get_program_scope()->put_bound_type(status, bound_tag_type);
			if (!!status) {
				bound_var_t::ref tag = llvm_create_global_tag(
						builder, scope, bound_tag_type, fqn_tag_name,
						make_code_id(token));

				/* record this tag variable for use later */
				scope->put_bound_variable(status, tag_name, tag);

				if (!!status) {
					debug_above(7, log(log_info, "instantiated nullary data ctor %s",
								tag->str().c_str()));
					return;
				}
			}
		}
	}

	assert(!status);
	return;
}

void ast::type_def_t::resolve_statement(
		status_t &status,
		llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
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

	return;
}

bound_var_t::ref type_check_assignment(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bound_var_t::ref lhs_var,
		bound_var_t::ref rhs_var,
		location_t location)
{
	if (!lhs_var->is_ref()) {
		user_error(status, location,
				"you cannot assign to a variable that is immutable. "
				" %s is not a ref", lhs_var->str().c_str());
	}

	if (!!status) {
		indent_logger indent(5, string_format(
					"type checking assignment %s = %s",
					lhs_var->str().c_str(),
					rhs_var->str().c_str()));

		auto lhs_unreferenced_type = dyncast<const types::type_ref_t>(lhs_var->type->get_type())->element_type;
		bound_type_t::ref lhs_unreferenced_bound_type = upsert_bound_type(status, builder, scope, lhs_unreferenced_type);

		if (!!status) {
			unification_t unification = unify(
					lhs_unreferenced_type,
					rhs_var->type->get_type(), scope->get_typename_env());

			if (unification.result) {
				/* ensure that whatever was being pointed to by this LHS
				 * is released after this statement */
				auto prior_lhs_value = lhs_var->resolve_bound_value(status, builder, scope);

				if (!!status) {
					// TODO: handle assignments to member variables
					assert(llvm::dyn_cast<llvm::AllocaInst>(lhs_var->get_llvm_value())
							|| llvm::dyn_cast<llvm::GlobalVariable>(lhs_var->get_llvm_value())
                            || llvm_value_is_pointer(lhs_var->get_llvm_value()));

					builder.CreateStore(
							llvm_maybe_pointer_cast(builder,
								rhs_var->resolve_bound_var_value(builder),
								lhs_unreferenced_bound_type->get_llvm_specific_type()),
							lhs_var->get_llvm_value());

					if (!!status) {
						if (rhs_var->type->is_managed_ptr(scope)) {
							if (rhs_var->get_llvm_value() != prior_lhs_value->get_llvm_value()) {
								/* only bother addref/release if these are different things */
								call_addref_var(status, builder, scope, rhs_var, "addref after assignment");
								call_release_var(status, builder, scope, prior_lhs_value, "release after assignment");
							}
						}

						return lhs_var;
					}
				}
			} else {
				user_error(status, location, "left-hand side is incompatible with the right-hand side (%s)",
						unification.str().c_str());
			}
		}
	}

	assert(!status);
	return nullptr;
}

void ast::assignment_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	assert(token.text == "=");

	auto lhs_var = lhs->resolve_expression(status, builder, scope, life, true /*as_ref*/);
	if (!!status) {
		auto rhs_var = rhs->resolve_expression(status, builder, scope, life, false /*as_ref*/);
		type_check_assignment(status, builder, scope, life, lhs_var,
				rhs_var, token.location);
		return;
	}

	assert(!status);
	return;
}

void ast::break_flow_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	if (auto runnable_scope = dyncast<runnable_scope_t>(scope)) {
		llvm::BasicBlock *break_bb = runnable_scope->get_innermost_loop_break();
		if (break_bb != nullptr) {
			assert(!builder.GetInsertBlock()->getTerminator());

			/* release everything held back to the loop we're in */
			life->release_vars(status, builder, scope, lf_loop);

			builder.CreateBr(break_bb);
			return;
		} else {
			user_error(status, get_location(), c_control("break") " outside of a loop");
		}
	} else {
		panic("we should not be looking at a break statement here!");
	}
	assert(!status);
	return;
}

void ast::continue_flow_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	if (auto runnable_scope = dyncast<runnable_scope_t>(scope)) {
		llvm::BasicBlock *continue_bb = runnable_scope->get_innermost_loop_continue();
		if (continue_bb != nullptr) {
			assert(!builder.GetInsertBlock()->getTerminator());

			/* release everything held back to the loop we're in */
			life->release_vars(status, builder, scope, lf_loop);

			builder.CreateBr(continue_bb);
			return;
		} else {
			user_error(status, get_location(), c_control("continue") " outside of a loop");
		}
	} else {
		panic("we should not be looking at a continue statement here!");
	}
	assert(!status);
	return;
}

bound_var_t::ref type_check_binary_op_assignment(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		ast::item_t::ref op_node,
		ast::expression_t::ref lhs,
		ast::expression_t::ref rhs,
		location_t location,
		atom function_name)
{
	auto lhs_var = lhs->resolve_expression(status, builder, scope, life, true /*as_ref*/);
	if (!!status) {
		bound_var_t::ref lhs_val = lhs_var->resolve_bound_value(status, builder, scope);

		if (!!status) {
			auto rhs_var = rhs->resolve_expression(status, builder, scope, life, false /*as_ref*/);

			if (!!status) {
				auto computed_var = call_program_function(status, builder, scope,
						life, function_name, op_node, {lhs_val, rhs_var});

				return type_check_assignment(status, builder, scope, life, lhs_var,
						computed_var, location);
			}
		}
	}

	assert(!status);
	return nullptr;
}

void ast::mod_assignment_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	type_check_binary_op_assignment(status, builder, scope, life,
			shared_from_this(), lhs, rhs, token.location, "__mod__");
}

void ast::plus_assignment_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	type_check_binary_op_assignment(status, builder, scope, life,
			shared_from_this(), lhs, rhs, token.location, "__plus__");
}

void ast::minus_assignment_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	type_check_binary_op_assignment(status, builder, scope, life,
			shared_from_this(), lhs, rhs, token.location, "__minus__");
}

void ast::return_statement_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	life = life->new_life(status, lf_statement);

	/* obviously... */
	*returns = true;

	/* let's figure out if we have a return value, and what it's type is */
    bound_var_t::ref return_value;
    bound_type_t::ref return_type;

	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
	assert(runnable_scope != nullptr);

	auto return_type_constraint = runnable_scope->get_return_type_constraint();

    if (expr != nullptr) {
        /* if there is a return expression resolve it into a value. also, be
		 * sure to retain whether the function signature necessitates a ref type */
		return_value = expr->resolve_expression(status, builder, scope, life,
				return_type_constraint ? return_type_constraint->is_ref() : false /*as_ref*/);

		/* addref this return value on behalf of the caller */
		call_addref_var(status, builder, scope, return_value, "return statement");

        if (!!status) {
            /* get the type suggested by this return value */
            return_type = return_value->type;
        }
    } else {
        /* we have an empty return, let's just use void */
        return_type = scope->get_program_scope()->get_bound_type({"void"});
    }

    if (!!status) {
		/* release all variables from all lives */
		life->release_vars(status, builder, scope, lf_function);

		/* make sure this return type makes sense, or keep track of it if we
		 * didn't yet know the return type for this function */
		runnable_scope->check_or_update_return_type_constraint(status,
				shared_from_this(), return_type);

		if (return_value != nullptr) {
			auto llvm_return_value = llvm_maybe_pointer_cast(builder,
					return_value->resolve_bound_var_value(builder),
					runnable_scope->get_return_type_constraint());

			if (llvm_return_value->getName().str().size() == 0) {
				llvm_return_value->setName("return.value");
			}
			debug_above(8, log("emitting a return of %s", llvm_print(llvm_return_value).c_str()));

			// TODO: release live variables in scope, except the one being
			// returned
			builder.CreateRet(llvm_return_value);
		} else {
			assert(types::is_type_id(return_type->get_type(), "void"));

			// TODO: release live variables in scope
			builder.CreateRetVoid();
		}

        return;
    }
    assert(!status);
    return;
}

void ast::times_assignment_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	type_check_binary_op_assignment(status, builder, scope, life,
			shared_from_this(), lhs, rhs, token.location, "__times__");
}

void ast::divide_assignment_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	type_check_binary_op_assignment(status, builder, scope, life,
			shared_from_this(), lhs, rhs, token.location, "__divide__");
}

void ast::block_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
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

	/* create a new life for tracking value lifetimes across this block */
	life = life->new_life(status, lf_block);

	for (auto &statement : statements) {
		if (*returns) {
			user_error(status, *statement, "this statement will never run");
			break;
		}

		local_scope_t::ref next_scope;

		debug_above(9, log(log_info, "type checking statement\n%s", statement->str().c_str()));

		/* create a new life for tracking the rhs values (temp values) in this statement */
		auto stmt_life = life->new_life(status, lf_statement);

		{
			indent_logger indent(5, string_format(
						"while checking statement at %s",
						statement->get_location().str().c_str()));

			/* resolve the statement */
			statement->resolve_statement(status, builder, current_scope, stmt_life,
					&next_scope, returns);
		}

		if (!*returns) {
			/* inject release operations for rhs values out of extent */
			stmt_life->release_vars(status, builder, scope, lf_statement);
		}

		if (!!status) {
			if (next_scope != nullptr) {
				/* the statement just executed wants to create a new nested scope.
				 * let's allow this by just keeping track of the current scope. */
				current_scope = next_scope;
				next_scope = nullptr;
				debug_above(10, log(log_info, "got a new scope %s", current_scope->str().c_str()));
			}
		}

		if (!status) {
			if (!status.reported_on_error_at(statement->get_location())) {
				user_error(status, statement->get_location(), "while checking statement");
			}
			break;
		}
    }

	if (!*returns) {
		/* if the block ensured that all code paths returned, then the lifetimes
		 * of the related objects was managed. otherwise, let's do it here. */
		life->release_vars(status, builder, scope, lf_block);
	}

    return;
}

struct for_like_var_decl_t : public ast::like_var_decl_t {
	atom symbol;
	location_t location;
	const ast::expression_t &collection;
	types::type_t::ref type;

	for_like_var_decl_t(atom symbol, location_t location, const ast::expression_t &collection) :
		symbol(symbol), location(location), collection(collection), type(::type_variable(location))
	{
	}

	virtual atom get_symbol() const {
		return symbol;
	}

	virtual location_t get_location() const {
		return location;
	}

	virtual types::type_t::ref get_type() const {
		return type;
	}

	virtual bool has_initializer() const {
		return true;
	}

	virtual bound_var_t::ref resolve_initializer(
			status_t &status,
			llvm::IRBuilder<> &builder,
			scope_t::ref scope,
			life_t::ref life) const
	{
		return null_impl();
	}
};

void ast::for_block_t::resolve_statement(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		local_scope_t::ref *new_scope,
		bool *returns) const
{
	auto maybe_step_symbol = types::gensym();

	bound_var_t::ref maybe_step = type_check_bound_var_decl(
			status,
			builder,
			scope,
			for_like_var_decl_t(
				maybe_step_symbol->get_name(),
				maybe_step_symbol->get_location(),
				*collection),
			life,
			false /*maybe_unbox*/);

	not_impl();

	assert(!status);
	return;
}

void ast::while_block_t::resolve_statement(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
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

		/* demarcate a loop boundary here */
		life = life->new_life(status, lf_loop);

		auto cond_life = life->new_life(status, lf_statement);

		/* evaluate the condition for branching */
		bound_var_t::ref condition_value = condition->resolve_expression(
				status, builder, scope, cond_life, false /*as_ref*/);

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
				llvm::BasicBlock *while_end_bb = llvm::BasicBlock::Create(builder.getContext(), "while.end");

				/* keep track of the "break" and "continue" jump locations */
				loop_tracker_t loop_tracker(dyncast<runnable_scope_t>(scope), while_cond_bb, while_end_bb);

				/* we don't have an else block, so we can just continue on */
				llvm_create_if_branch(status, builder, scope,
						IFF_ELSE, cond_life, llvm_raw_condition_value,
						while_block_bb, while_end_bb);

				if (!!status) {
					assert(builder.GetInsertBlock()->getTerminator());

					/* let's generate code for the "then" block */
					builder.SetInsertPoint(while_block_bb);
					assert(!builder.GetInsertBlock()->getTerminator());

					llvm::Value *llvm_bool_overload_value = maybe_get_bool_overload_value(status,
							builder, scope, cond_life, condition, condition_value);

					if (!!status) {
						if (llvm_bool_overload_value != nullptr) {
							/* we've got a second condition to check, let's do it */
							auto deep_while_bb = llvm::BasicBlock::Create(builder.getContext(),
									"deep-while", llvm_function_current);

							llvm_create_if_branch(status, builder, scope,
									IFF_BOTH, cond_life,
									llvm_bool_overload_value, deep_while_bb,
									while_end_bb);
							builder.SetInsertPoint(deep_while_bb);
						} else {
							cond_life->release_vars(status, builder, scope, lf_statement);
						}

						if (!!status) {
							block->resolve_statement(status, builder,
									while_scope ? while_scope : scope, life, nullptr,
									nullptr);

							/* the loop can't store values */
							assert(life->values.size() == 0 && life->life_form == lf_loop);

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
								return;
							}
						}
					}
				}
			}
		}
	} else {
		/* this should never happen */
		not_impl();
	}

    assert(!status);
    return;
}

void ast::if_block_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
	assert(life->life_form == lf_statement);

	/* if scope allows us to set up new variables inside if conditions */
	local_scope_t::ref if_scope;

	bool if_block_returns = false, else_block_returns = false;

	assert(token.text == "if" || token.text == "elif");
	bound_var_t::ref condition_value;

	auto cond_life = life->new_life(status, lf_statement);

	/* evaluate the condition for branching */
	if (auto var_decl = dyncast<const ast::var_decl_t>(condition)) {
		/* our user is attempting an assignment inside of an if statement, let's
		 * grant them a favor, and automatically unbox the Maybe type if it
		 * exists. */
		condition_value = var_decl->resolve_as_condition(
				status, builder, scope, cond_life, &if_scope);
	} else if (auto ref_expr = dyncast<const ast::reference_expr_t>(condition)) {
		condition_value = ref_expr->resolve_as_condition(
				status, builder, scope, cond_life, &if_scope);
	} else if (auto expr = dyncast<const ast::expression_t>(condition)) {
		condition_value = expr->resolve_expression(
				status, builder, scope, cond_life, false /*as_ref*/);
	} else {
		panic("how did this get here?");
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
	 * class...
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

			/* we have to keep track of whether we need a merge block
			 * because our nested branches could all return */
			bool insert_merge_bb = false;

			llvm::BasicBlock *else_bb = llvm::BasicBlock::Create(builder.getContext(), "else", llvm_function_current);

			/* put the merge block after the else block */
			llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(builder.getContext(), "ifcont");

			/* create the actual branch instruction */
			llvm_create_if_branch(status, builder, scope, IFF_ELSE, cond_life,
					llvm_raw_condition_value, then_bb, else_bb);

			if (!!status) {
				builder.SetInsertPoint(else_bb);

				if (else_ != nullptr) {
					else_->resolve_statement(status, builder, scope, life,
							nullptr, &else_block_returns);
				}

				if (!else_block_returns) {
					/* keep track of the fact that we have to have a
					 * merged block to land in after the else block */
					insert_merge_bb = true;

					/* go ahead and jump there */
					if (!builder.GetInsertBlock()->getTerminator()) {
						builder.CreateBr(merge_bb);
					}
				}

				if (!!status) {
					/* let's generate code for the "then" block */
					builder.SetInsertPoint(then_bb);
					llvm::Value *llvm_bool_overload_value = maybe_get_bool_overload_value(status,
							builder, scope, cond_life, condition, condition_value);

					if (!!status) {
						if (llvm_bool_overload_value != nullptr) {
							/* we've got a second condition to check, let's do it */
							auto deep_then_bb = llvm::BasicBlock::Create(builder.getContext(), "deep-then", llvm_function_current);

							llvm_create_if_branch(status, builder, scope,
									IFF_THEN | IFF_ELSE, cond_life,
									llvm_bool_overload_value, deep_then_bb,
									else_bb ? else_bb : merge_bb);
							builder.SetInsertPoint(deep_then_bb);
						} else {
							cond_life->release_vars(status, builder, scope, lf_statement);
						}

						if (!!status) {
							block->resolve_statement(status, builder,
									if_scope ? if_scope : scope, life, nullptr, &if_block_returns);

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
								return;
							}
						}
					}
				}
			}
		}
	}

	assert(!status);
    return;
}

bound_var_t::ref ast::bang_expr_t::resolve_expression(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);

	auto lhs_value = lhs->resolve_expression(status, builder, scope, life,
			false /*as_ref*/);

	if (!!status) {
		auto type = lhs_value->type->get_type();
		auto maybe_type = dyncast<const types::type_maybe_t>(type);
		if (maybe_type != nullptr) {
			bound_type_t::ref just_bound_type = upsert_bound_type(status,
					builder, scope, maybe_type->just);

			return bound_var_t::create(INTERNAL_LOC(), lhs_value->name,
					just_bound_type,
					lhs_value->get_llvm_value(),
					lhs_value->id);
		} else {
			user_error(status, *this, "bang expression is unnecessary since this is not a 'maybe' type: %s",
					type->str().c_str());
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::var_decl_t::resolve_as_condition(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
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
            status, builder, fresh_scope, *this, life, true /*maybe_unbox*/);

	if (!!status) {
		*new_scope = fresh_scope;
		return var_decl_value;
	}

	assert(!status);
	return nullptr;
}

void ast::var_decl_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		local_scope_t::ref *new_scope,
		bool * /*returns*/) const
{
	if (auto runnable_scope = dyncast<runnable_scope_t>(scope)) {
		/* variable declarations begin new scopes */
		local_scope_t::ref fresh_scope = runnable_scope->new_local_scope(
				string_format("variable-%s", token.text.c_str()));

		scope = fresh_scope;

		/* check to make sure this var decl is sound */
		bound_var_t::ref var_decl_value = type_check_bound_var_decl(
				status, builder, fresh_scope, *this, life, false /*maybe_unbox*/);

		if (!!status) {
			*new_scope = fresh_scope;
			return;
		}
	} else {
		panic("we should not be trying to instantiate a var decl outside of a runnable scope");
	}

	assert(!status);
	return;
}

void ast::pass_flow_t::resolve_statement(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        local_scope_t::ref *new_scope,
		bool *returns) const
{
    return;
}

bound_var_t::ref ast::times_expr_t::resolve_expression(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
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

	return type_check_binary_operator(status, builder, scope, life, lhs, rhs,
			shared_from_this(), function_name);
}

bound_var_t::ref ast::prefix_expr_t::resolve_expression(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
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
	bound_var_t::ref rhs_var = rhs->resolve_expression(status, builder,
			scope, life, false /*as_ref*/);

    if (!!status) {
		return call_program_function(status, builder, scope, life,
				function_name, shared_from_this(), {rhs_var});
    }
    assert(!status);
    return nullptr;
}

bound_var_t::ref ast::literal_expr_t::resolve_expression(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
    scope_t::ref program_scope = scope->get_program_scope();

    switch (token.tk) {
	case tk_raw_integer:
        {
			/* create a native integer */
			int64_t value = atoll(token.text.substr(0, token.text.size() - 1).c_str());
			bound_type_t::ref native_type = program_scope->get_bound_type({INT_TYPE});
			return bound_var_t::create(
					INTERNAL_LOC(), "raw_int_literal", native_type,
					llvm_create_int(builder, value),
					make_code_id(token));
        }
		break;
    case tk_integer:
        {
			/* create a boxed integer */
            int64_t value = atoll(token.text.c_str());
            bound_type_t::ref native_type = program_scope->get_bound_type({INT_TYPE});
			bound_type_t::ref boxed_type = upsert_bound_type(
					status,
					builder,
					scope,
					type_id(make_iid("std/int")));
			if (!!status) {
				assert(boxed_type != nullptr);
				bound_var_t::ref box_int = get_callable(
						status,
						builder,
						scope,
						{"int"},
						shared_from_this(),
						scope->get_outbound_context(),
						get_args_type({native_type}));

				if (!!status) {
					assert(box_int != nullptr);
					return create_callsite(
							status,
							builder,
							scope,
							life,
							box_int,
							{string_format("literal int (%d)", value)},
							get_location(),
							{bound_var_t::create(
									INTERNAL_LOC(), "temp_int_literal", boxed_type,
									llvm_create_int(builder, value),
									make_code_id(token))});
				}
			}
        }
		break;
    case tk_raw_string:
		{
			std::string value = unescape_json_quotes(token.text.substr(0, token.text.size() - 1));
			bound_type_t::ref native_type = program_scope->get_bound_type({STR_TYPE});
			return bound_var_t::create(
					INTERNAL_LOC(), "raw_str_literal", native_type,
					llvm_create_global_string(builder, value),
					make_code_id(token));
		}
		break;
    case tk_string:
		{
			std::string value = unescape_json_quotes(token.text);
			bound_type_t::ref native_type = program_scope->get_bound_type({STR_TYPE});
			bound_type_t::ref boxed_type = upsert_bound_type(
					status,
					builder,
					scope,
					type_id(make_iid("std/str")));

			if (!!status) {
				assert(boxed_type != nullptr);
				bound_var_t::ref box_str = get_callable(
						status,
						builder,
						scope,
						{"__box__"},
						shared_from_this(),
						scope->get_outbound_context(),
						get_args_type({native_type}));

				if (!!status) {
					return create_callsite(
							status,
							builder,
							scope,
							life,
							box_str,
							{string_format("literal str (%s)", value.c_str())},
							get_location(),
							{bound_var_t::create(
									INTERNAL_LOC(), "temp_str_literal", boxed_type,
									llvm_create_global_string(builder, value),
									make_code_id(token))});
				}
			}
		}
		break;
	case tk_raw_float:
		{
			double value = atof(token.text.substr(0, token.text.size() - 1).c_str());
			bound_type_t::ref native_type = program_scope->get_bound_type({FLOAT_TYPE});
			return bound_var_t::create(
					INTERNAL_LOC(), "raw_float_literal", native_type,
					llvm_create_double(builder, value),
					make_code_id(token));
		}
		break;
	case tk_float:
		{
			double value = atof(token.text.c_str());
			bound_type_t::ref native_type = program_scope->get_bound_type({FLOAT_TYPE});
			bound_type_t::ref boxed_type = upsert_bound_type(
					status,
					builder,
					scope,
					type_id(make_iid("std/float")));
			if (!!status) {
				assert(boxed_type != nullptr);
				bound_var_t::ref box_float = get_callable(
						status,
						builder,
						scope,
						{"float"},
						shared_from_this(),
						scope->get_outbound_context(),
						get_args_type({native_type}));

				if (!!status) {
					return create_callsite(
							status,
							builder,
							scope,
							life,
							box_float,
							{string_format("literal float (%f)", value)},
							get_location(),
							{bound_var_t::create(
									INTERNAL_LOC(), "temp_float_literal", boxed_type,
									llvm_create_double(builder, value),
									make_code_id(token))});
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

bound_var_t::ref ast::reference_expr_t::resolve_overrides(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		const ptr<const ast::item_t> &callsite,
		const bound_type_t::refs &args) const
{
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

bound_var_t::ref ast::cast_expr_t::resolve_expression(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		life_t::ref life,
		bool as_ref) const
{
	assert(!as_ref);

	bound_var_t::ref bound_var = lhs->resolve_expression(status, builder, scope, life, false /*as_ref*/);
	if (!!status) {
		bound_type_t::ref bound_type = upsert_bound_type(status, builder, scope, type_cast);
		if (!!status) {
			// TODO: consider checking for certain casting
			llvm::Value *llvm_var_value = llvm_maybe_pointer_cast(builder,
					bound_var->resolve_bound_var_value(builder), bound_type);

			return bound_var_t::create(INTERNAL_LOC(), "cast",
					bound_type, llvm_var_value, make_iid("cast"));
		}
	}

	assert(!status);
	return nullptr;
}

void dump_builder(llvm::IRBuilder<> &builder) {
	std::cerr << llvm_print_function(llvm_get_function(builder)) << std::endl;
}
