#include "zion.h"
#include "atom.h"
#include "logger.h"
#include "type_checker.h"
#include "utils.h"
#include "callable.h"
#include "compiler.h"
#include "llvm_zion.h"
#include "llvm_utils.h"
#include "ast.h"
#include "llvm_types.h"
#include "parser.h"
#include "unification.h"
#include "code_id.h"
#include "patterns.h"
#include <iostream>
#include "type_kind.h"
#include "null_check.h"
#include <time.h>
#include "coercions.h"

/*
 * The basic idea here is that type checking is a graph operation which can be
 * ordered topologically based on dependencies between callers and callees.
 * Luckily our AST has exactly that structure.  We will perform a topological
 * sort by resolving types as we return from our depth first traversal.
 */


/************************************************************************/

bound_type_t::ref get_fully_bound_param_info(
		llvm::IRBuilder<> &builder,
		const ast::var_decl_t &obj,
		scope_t::ref scope,
		std::string &var_name,
		std::set<std::string> &generics,
		int &generic_index)
{
	/* get the name of this parameter */
	var_name = obj.token.text;

	assert(obj.type != nullptr);

	/* the user specified a type */
	debug_above(6, log(log_info, "upserting type for param %s at %s",
				obj.type->str().c_str(),
				obj.type->get_location().str().c_str()));
	return upsert_bound_type(builder, scope, obj.type);
}


llvm::Value *resolve_init_var(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		const ast::var_decl_t &obj,
		const std::string &symbol,
		types::type_t::ref declared_type,
		llvm::Function *llvm_function,
		bound_var_t::ref init_var,
		bound_type_t::ref value_type,
		bool is_managed)
{
	/* assumption here is that init_var has already been unified against the declared type.
	 * if this function returns an AllocaInst then that will imply that the variable should be
	 * treated as a ref that can be changed. */
	llvm::AllocaInst *llvm_alloca;
	if (is_managed) {
		/* we need stack space, and we have to track it for garbage collection */
		llvm_alloca = llvm_call_gcroot(llvm_function, value_type, symbol);
	} else if (obj.is_let()) {
		/* we don't need a stack var */
		llvm_alloca = nullptr;
	} else {
		/* we need some stack space because this name is mutable */
		llvm_alloca = llvm_create_entry_block_alloca(llvm_function, value_type, symbol);
	}

	if (init_var == nullptr) {
		if (declared_type->eval_predicate(tb_maybe, scope)) {
			/* this can be null, and we do not allow user-defined __init__ for maybe types, so let's initialize it as null */
			llvm::Constant *llvm_null_value = llvm::Constant::getNullValue(value_type->get_llvm_specific_type());
			if (llvm_alloca == nullptr) {
				return llvm_null_value;
			} else {
				if (obj.is_let()) {
					throw user_error(obj.get_location(), "you might as well just use " c_id("null") " rather than declaring this uninitialized maybe");
				}

				assert(llvm_alloca != nullptr);
				builder.CreateStore(llvm_null_value, llvm_alloca);
				return llvm_alloca;
			}
		} else {
			/* this is not a maybe type */

			try {
				/* the user didn't supply an initializer, let's see if this type has one */
				bound_var_t::ref init_fn = get_callable(
						builder,
						scope->get_module_scope(),
						"__init__",
						obj.get_location(),
						type_args({}, {}),
						value_type->get_type());
				init_var = make_call_value(builder, obj.get_location(), scope,
						life, init_fn, {} /*arguments*/);
			} catch (user_error &e) {
				std::throw_with_nested(user_error(obj.get_location(), "missing initializer"));
			}
		}
	}

	assert(init_var != nullptr);
	llvm::Value *llvm_init_value;
	if (!init_var->type->get_type()->eval_predicate(tb_null, scope)) {
		llvm_init_value = coerce_value(builder, scope, life,
				obj.get_location(), value_type->get_type(), init_var);
	} else {
		llvm_init_value = llvm::Constant::getNullValue(value_type->get_llvm_specific_type());
	}

	assert(llvm_init_value != nullptr);
	if (llvm_init_value->getName().size() == 0) {
		llvm_init_value->setName(string_format("%s.initializer", symbol.c_str()));
	}

	if (llvm_alloca == nullptr) {
		/* this is a native 'let' */
		assert(obj.is_let());
		assert(!is_managed);
		return llvm_init_value;
	} else {
		debug_above(6, log(log_info, "creating a store instruction %s := %s",
					llvm_print(llvm_alloca).c_str(),
					llvm_print(llvm_init_value).c_str()));

		builder.CreateStore(llvm_init_value, llvm_alloca);
		if (obj.is_let()) {
			/* this is a managed 'let' */
			assert(is_managed);
			return builder.CreateLoad(llvm_alloca);
		} else {
			/* this is a native or managed 'var' */
			return llvm_alloca;
		}
	}
}

bound_var_t::ref generate_stack_variable(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		const ast::var_decl_t &var_decl,
		const std::string &symbol,
		types::type_t::ref declared_type,
		bool maybe_unbox)
{
	/* 'init_var' is keeping track of the value we are assigning to our new
	 * variable (if any exists.) */
	bound_var_t::ref init_var;

	/* only check initializers inside a runnable scope */
	assert(dyncast<runnable_scope_t>(scope) != nullptr);

	if (var_decl.initializer != nullptr) {
		/* we have an initializer */
		init_var = var_decl.initializer->resolve_expression(builder, scope, life, false /*as_ref*/, declared_type);
		if (init_var->type->is_void(scope)) {
			throw user_error(var_decl.get_location(),
					"cannot initialize a variable with void, since it has no value");
		}
	}

	/* 'stack_var_type' is keeping track of what the stack variable's type will be (hint: it should
	 * just be a ref to the value_type) */
	bound_type_t::ref stack_var_type;

	/* 'value_type' is keeping track of what the variable's ending type will be */
	bound_type_t::ref value_type;

	/* 'unboxed' tracks whether we are doing maybe unboxing for this var_decl */
	bool unboxed = false;

	if (init_var != nullptr) {
		/* we have an initializer */
		if (declared_type != nullptr) {
			/* ensure 'init_var' <: 'declared_type' */
			unification_t unification = unify(
					declared_type,
					init_var->get_type(),
					scope);

			if (unification.result) {
				/* the lhs is a supertype of the rhs */
				declared_type = declared_type->rebind(unification.bindings);
				debug_above(7, log_location(log_info, var_decl.get_location(),
					   	"initializer %s (%s) unifies with declared type %s",
						var_decl.initializer->str().c_str(),
						init_var->str().c_str(),
						declared_type->str().c_str()));
			} else {
				/* report that the variable type does not match the initializer type */
				auto error = user_error(var_decl.get_location(),
						"declared type of `" c_var("%s") "` does not match type of initializer",
						var_decl.get_symbol().c_str());
				error.add_info(init_var->get_location(), c_type("%s") " != " c_type("%s") " because %s",
						declared_type->str().c_str(),
						init_var->type->str().c_str(),
						unification.reasons.c_str());
				throw error;
			}
		} else {
			/* we must get the type from the initializer */
			declared_type = init_var->type->get_type();
		}
	}

	assert(declared_type != nullptr);
	declared_type = declared_type->eval(scope);

	if (maybe_unbox) {
		debug_above(3, log(log_info, "attempting to unbox %s", var_decl.get_symbol().c_str()));

		/* try to see if we can unbox this if it's a Maybe */
		if (init_var == nullptr) {
			throw user_error(var_decl.get_location(), "missing initialization value");
		} else {
			/* since we are maybe unboxing, then let's first off see if
			 * this is even a maybe type. */
			if (auto maybe_type = dyncast<const types::type_maybe_t>(declared_type)) {
				/* looks like the initialization variable is a supertype
				 * of the null type */
				unboxed = true;

				stack_var_type = upsert_bound_type(builder, scope,
						type_ref(maybe_type->just));
				value_type = upsert_bound_type(builder, scope, maybe_type->just);
			} else {
				/* this is not a maybe, so let's just move along */
			}
		}
	}

	if (stack_var_type == nullptr) {
		stack_var_type = upsert_bound_type(builder, scope, type_ref(declared_type));
		value_type = upsert_bound_type(builder, scope, declared_type);
	}

	/* generate the mutable stack-based variable for this var */
	llvm::Function *llvm_function = llvm_get_function(builder);

	// NOTE: we don't make this a gcroot until a little later on
	bool is_managed;
	value_type->is_managed_ptr(builder, scope, is_managed);
	llvm::Value *llvm_value = nullptr;
	llvm_value = resolve_init_var(builder, scope, life, var_decl, symbol, declared_type,
			llvm_function, init_var, value_type, is_managed);

	/* the reference_expr that looks at this llvm_value will need to
	 * know to use store/load semantics, not just pass-by-value */
	bound_var_t::ref var_decl_variable = bound_var_t::create(
			INTERNAL_LOC(),
			symbol,
			llvm::dyn_cast<llvm::AllocaInst>(llvm_value) ? stack_var_type : value_type,
			llvm_value,
			make_type_id_code_id(var_decl.get_location(), var_decl.get_symbol()));

	/* memory management */
	life->track_var(builder, scope, var_decl_variable, lf_block);

	/* on our way out, stash the variable in the current scope */
	scope->put_bound_variable(var_decl_variable->name, var_decl_variable);

	if (unboxed) {
		/* 'condition_value' refers to whether this was an unboxed maybe */
		bound_var_t::ref condition_value;

		assert(init_var != nullptr);
		assert(maybe_unbox);

		/* get the maybe type so that we can use it as a conditional */
		bound_type_t::ref condition_type = upsert_bound_type(builder, scope, declared_type);
		llvm::Value *llvm_resolved_value = init_var->resolve_bound_var_value(scope, builder);

		/* we're unboxing a Maybe{any}, so let's return
		 * whether this was Nothing or not... */
		return bound_var_t::create(INTERNAL_LOC(), symbol,
				condition_type, llvm_resolved_value,
				make_type_id_code_id(var_decl.get_location(), var_decl.get_symbol()));
	} else {
		return var_decl_variable;
	}
}

bound_var_t::ref upsert_module_variable(
		llvm::IRBuilder<> &builder,
		module_scope_t::ref module_scope,
		const ast::var_decl_t &var_decl,
		std::string symbol)
{
	auto program_scope = module_scope->get_program_scope();

	/* 'declared_type' tells us the user-declared type on the left-hand side of
	 * the assignment. */
	types::type_t::ref declared_type = var_decl.type->rebind(module_scope->get_type_variable_bindings());
	if (declared_type == nullptr || declared_type->ftv_count() != 0) {
		throw user_error(var_decl.get_location(), "module variables must have explicitly declared types");
		return nullptr;
	}

	assert(declared_type != nullptr);
	bound_type_t::ref bound_type = upsert_bound_type(builder, module_scope, declared_type);

	auto bound_global_type = upsert_bound_type(builder, module_scope, type_ref(declared_type));

	bound_var_t::ref already_bound_var;
	if (module_scope->has_bound(symbol, type_ref(declared_type), &already_bound_var)) {
		return already_bound_var;
	}

	llvm::Constant *llvm_constant = nullptr;
	if (bound_type->get_llvm_specific_type()->isPointerTy()) {
		llvm_constant = llvm::Constant::getNullValue(bound_type->get_llvm_specific_type());
	} else if (bound_type->get_llvm_specific_type()->isIntegerTy()) {
		llvm_constant = llvm::ConstantInt::get(bound_type->get_llvm_specific_type(), 0, false);
	} else {
		throw user_error(var_decl.get_location(), "unsupported type for module variable %s",
				bound_type->str().c_str());
	}

	llvm::Module *llvm_module = module_scope->get_llvm_module();
	llvm::GlobalVariable *llvm_global_variable = llvm_get_global(
			llvm_module,
			symbol,
			llvm_constant,
			false /*is_constant*/);

	bound_var_t::ref var_decl_variable = bound_var_t::create(INTERNAL_LOC(), symbol,
			bound_global_type, llvm_global_variable, make_code_id(var_decl.token));

	/* preemptively stash the variable in the module scope */
	module_scope->put_bound_variable(var_decl_variable->name,
			var_decl_variable);

	function_scope_t::ref function_scope = module_scope->new_function_scope(
			std::string("__init_module_vars_") + symbol);

	/* 'init_var' is keeping track of the value we are assigning to our new
	 * variable (if any exists.) */
	bound_var_t::ref init_var;

	llvm::IRBuilderBase::InsertPointGuard ipg(builder);
	program_scope->set_insert_point_to_init_module_vars_function(builder, var_decl.token.text);

	assert(llvm_get_function(builder) != nullptr);

	auto life = (
			make_ptr<life_t>(lf_function)
			->new_life(lf_block)
			->new_life(lf_statement));

	if (var_decl.initializer) {
		/* we have an initializer */
		init_var = var_decl.initializer->resolve_expression(builder,
				function_scope, life, false /*as_ref*/, declared_type);
	}

	if (init_var != nullptr) {
		/* we have an initializer */
		/* ensure 'init_var' <: 'declared_type' */
		unification_t unification = unify(declared_type, init_var->get_type(), module_scope);

		if (!unification.result) {
			/* report that the variable type does not match the initializer type */
			auto error = user_error(var_decl.get_location(), "declared type of `" c_var("%s") "` does not match type of initializer",
					var_decl.token.text.c_str());
			error.add_info(init_var->get_location(), c_type("%s") " != " c_type("%s") " because %s",
					declared_type->str().c_str(),
					init_var->type->str().c_str(),
					unification.reasons.c_str());
			throw error;
		}
	}

	if (init_var == nullptr) {
		/* the user didn't supply an initializer, let's see if this type has one */
		var_t::refs fns;
		fittings_t fittings;
		auto init_fn = maybe_get_callable(
				builder,
				module_scope,
				"__init__",
				var_decl.get_location(),
				type_args({}, {}),
				declared_type,
				fns,
				fittings);

		if (init_fn != nullptr) {
			init_var = make_call_value(builder, var_decl.get_location(),
					function_scope, life, init_fn, {} /*arguments*/);
		}
	}

	if (init_var != nullptr) {
		debug_above(6, log(log_info, "creating a store instruction %s := %s",
					llvm_print(llvm_global_variable).c_str(),
					llvm_print(init_var->get_llvm_value()).c_str()));

		llvm::Value *llvm_init_value = coerce_value(builder, module_scope, 
				life, var_decl.get_location(), declared_type, init_var);

		if (llvm_init_value->getName().str().size() == 0) {
			llvm_init_value->setName(string_format("%s.initializer", symbol.c_str()));
		}

		builder.CreateStore(
				llvm_maybe_pointer_cast(builder, llvm_init_value,
					bound_type->get_llvm_specific_type()),
				llvm_global_variable);
	} else {
		bool is_managed = false;
		var_decl_variable->type->is_managed_ptr(
				builder,
				module_scope,
				is_managed);

		if (is_managed) {
			if (!var_decl_variable->type->is_maybe(module_scope)) {
				throw user_error(var_decl.get_location(), "module var " c_id("%s") " missing initializer",
						symbol.c_str());
			}
		}
	}

	life->release_vars(
			builder,
			function_scope,
			lf_function);
	return var_decl_variable;
}

bound_var_t::ref type_check_bound_var_decl(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		const ast::var_decl_t &obj,
		life_t::ref life,
		bool maybe_unbox)
{
	const std::string symbol = obj.get_symbol();

	debug_above(4, log(log_info, "type_check_bound_var_decl is looking for a type for variable " c_var("%s") " : %s",
				symbol.c_str(), obj.get_symbol().c_str()));

	assert(dyncast<module_scope_t>(scope) == nullptr);
	auto runnable_scope = dyncast<runnable_scope_t>(scope);
	if (runnable_scope != nullptr) {
		bound_var_t::ref bound_var = runnable_scope->get_bound_variable(
				builder, obj.get_location(), symbol,
				runnable_scope->get_module_scope());

		if (bound_var != nullptr) {
			auto error = user_error(obj.get_location(), "symbol '" c_id("%s") "' cannot be redeclared",
					symbol.c_str());
			error.add_info(bound_var->get_location(), "see earlier declaration of " c_id("%s"),
					symbol.c_str());
			throw error;
		}
	}

	assert(obj.get_type() != nullptr);

	/* 'declared_type' tells us the user-declared type on the left-hand side of
	 * the assignment. this is generally used to allow a variable to be more
	 * generalized than the specific right-hand side initial value might be. */
	types::type_t::ref declared_type = obj.get_type()->rebind(scope->get_type_variable_bindings());

	assert(dyncast<runnable_scope_t>(scope) != nullptr);

	return generate_stack_variable(builder, scope, life,
			obj, symbol, declared_type, maybe_unbox);
}

bound_var_t::ref type_check_module_var_decl(
		llvm::IRBuilder<> &builder,
		module_scope_t::ref module_scope,
		const ast::var_decl_t &var_decl)
{
	auto program_scope = module_scope->get_program_scope();
	const std::string symbol = var_decl.token.text;

	debug_above(4, log(log_info, "type_check_module_var_decl is looking for a type for variable " c_var("%s") " : %s",
				symbol.c_str(), var_decl.str().c_str()));

	return upsert_module_variable(builder, module_scope, var_decl, symbol);
}

std::vector<std::string> get_param_list_decl_variable_names(ast::param_list_decl_t::ref obj) {
	std::vector<std::string> names;
	for (auto param : obj->params) {
		names.push_back({param->token.text});
	}
	return names;
}

bound_type_t::named_pairs zip_named_pairs(
		std::vector<std::string> names,
		bound_type_t::refs args)
{
	bound_type_t::named_pairs named_args;
	assert(names.size() == args.size());
	for (size_t i = 0; i < args.size(); ++i) {
		named_args.push_back({names[i], args[i]});
	}
	return named_args;
}

void destructure_function_details(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        location_t location,
        bool as_closure,
        types::type_function_t::ref &function_type,
        bool &needs_type_fixup,
        types::type_t::ref &type_constraints,
        bound_type_t::named_pairs &params,
        bound_type_t::ref &return_type)
{
	scope = scope->get_program_scope();
	type_constraints = function_type->type_constraints;

	/* the parameter types as per the decl */
	auto args = dyncast<const types::type_args_t>(function_type->args);
	assert(args != nullptr);
	if (as_closure) {
		/* an an implicit parameter to track the closure of the captured env */
		types::type_t::refs args_args = args->args;
		auto args_names = args->names;

		assert(args_names.size() == args_args.size());

		/* push the closure env */
		args_args.push_back(scope->get_program_scope()->get_runtime_type(builder, STD_MANAGED_TYPE, true /*get_ptr*/)->get_type());
		args_names.push_back(make_iid_impl("__env", location));

		args = type_args(args_args, args_names);
	}

	if (args->ftv_count() != 0) {
		throw unbound_type_error(args->get_location(), "because of the order of evaluation we could not eliminate the unbound type variables in %s. please add type annotations",
				args->str().c_str());
	}

	debug_above(6, log_location(log_info, args->get_location(), "args are %s", args->str().c_str()));
	bound_type_t::refs bound_args = upsert_bound_types(builder, scope, args->args);

	const auto &arg_names = args->names;
	dbg_when(arg_names.size() != bound_args.size());
	for (unsigned i = 0; i < bound_args.size(); ++i) {
		std::string param_name = arg_names[i]->get_name();
		params.push_back({param_name, bound_args[i]});
	}

	if (as_closure && function_type->return_type->ftv_count() != 0) {
		/* we are unsure at this point about what the return type is, but we're instantiating an
		 * anonymous closure which means recursion is off the table, so we don't really need to know
		 * our return type yet. fake it with LLVM until we discover it, then set it there */
		return_type = upsert_bound_type(builder, scope, type_unit());
		needs_type_fixup = true;
	} else {
		return_type = upsert_bound_type(builder, scope, function_type->return_type);
		needs_type_fixup = false;
	}

	auto implied_fn_type = get_function_type(type_constraints, params, return_type)->eval(scope);
	auto explicit_fn_type = function_type->eval(scope);

	if (!as_closure) {
		if (implied_fn_type->repr() != explicit_fn_type->repr()) {
			log_location(log_info, explicit_fn_type->get_location(),
					"%s should be %s (from %s) in scope %s",
					implied_fn_type->repr().c_str(),
					explicit_fn_type->repr().c_str(),
					function_type->str().c_str(),
				   	scope->get_name().c_str());
			dbg();
		}
	}

	function_type = dyncast<const types::type_function_t>(implied_fn_type);
	assert(function_type != nullptr);
}

void destructure_function_decl(
        llvm::IRBuilder<> &builder,
        const ast::function_decl_t &decl,
        scope_t::ref scope,
        types::type_t::ref &type_constraints,
        bool as_closure,
        bool &needs_type_fixup,
        bound_type_t::named_pairs &params,
        bound_type_t::ref &return_type,
        types::type_function_t::ref &function_type,
		types::type_t::ref expected_type)
{
	/* returns the parameters and the return value types fully resolved */
	debug_above(4, log(log_info, "type checking function decl %s with type %s in scope %s with type variables %s and expected type %s",
				decl.token.str().c_str(),
				decl.function_type->str().c_str(),
				scope->get_name().c_str(),
				::str(scope->get_type_variable_bindings()).c_str(),
				expected_type != nullptr ? expected_type->str().c_str() : "<null>"));

	assert_implies(as_closure, dyncast<closure_scope_t>(scope) != nullptr);

	types::type_t::ref type_declared_fn = decl.function_type->rebind(scope->get_type_variable_bindings());
	function_type = dyncast<const types::type_function_t>(type_declared_fn);
	if (as_closure) {
		if (function_type != nullptr) {
			throw user_error(decl.get_location(), "function expressions cannot have names (this one appears to be named " c_id("%s"),
					decl.token.text.c_str());
		}

		function_type = types::without_closure(type_declared_fn);
		expected_type = types::without_closure(expected_type);
	}

	assert(function_type != nullptr);
	if (expected_type != nullptr) {
		unification_t unification = unify(function_type, expected_type, scope);
		if (unification.result) {
			function_type = dyncast<const types::type_function_t>(function_type->rebind(unification.bindings));
		} /* else {
			auto error = user_error(decl.get_location(), "type definition does not meet expected type signature in this context");
			error.add_info(expected_type->get_location(), "expected type is %s", expected_type->str().c_str());
			error.add_info(function_type->get_location(), "declared type is %s", function_type->str().c_str());
			throw error;
		} */
	}

	destructure_function_details(
			builder,
			scope,
			decl.get_location(),
			as_closure,
			function_type,
			needs_type_fixup,
			type_constraints,
			params,
			return_type);
}


bool is_function_decl_generic(scope_t::ref scope, const ast::function_defn_t &obj) {
	return obj.decl
			->function_type
			->rebind(scope->get_type_variable_bindings())
			->ftv_count() != 0;
}

void ast::expression_t::resolve_statement(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	/* expressions as statements just pass through to evaluating the expr */
	resolve_expression(builder, scope, life, false /*as_ref*/, nullptr);
}

void ast::link_module_statement_t::resolve_statement(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	module_scope_t::ref module_scope = dyncast<module_scope_t>(scope);
	assert(module_scope != nullptr);

	auto linked_module_name = extern_module->get_canonical_name();
	assert(linked_module_name.size() != 0);

	program_scope_t::ref program_scope = scope->get_program_scope();
	module_scope_t::ref linked_module_scope = program_scope->lookup_module(linked_module_name);

	if (linked_module_scope != nullptr) {
		for (auto symbol : symbols) {
			/* get all the unchecked or things from the linked_module_scope and shove them into this
			 * module scope */
			linked_module_scope->copy_symbol(builder, symbol->get_location(), symbol->get_name(), module_scope);
		}

		/* put the module into program scope as a named variable. this is to
		 * enable dot-expressions to resolve module scope lookups. note that
		 * the module variables are not reified into the actual generated LLVM
		 * IR.  they are resolved entirely at compile time.  perhaps in a
		 * future version they can be used as run-time variables, so that we
		 * can pass modules around for another level of polymorphism. */
		bound_module_t::ref module_variable = bound_module_t::create(INTERNAL_LOC(),
				linked_module_name, make_code_id(token), linked_module_scope);

		module_scope->put_bound_variable(link_as_name.text, module_variable);
	} else {
		if (symbols.size() != 0) {
			throw user_error(token.location, "cannot import symbols from %s. it could be that the referenced module doesn't exist, or is declared global",
					linked_module_name.c_str());
		}
		/* some modules may not create a module scope if they are marked as global modules */
		return;
	}
}

bound_var_t::ref ast::link_var_statement_t::resolve_expression(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	assert(!as_ref);
	assert(expected_type == nullptr);

	module_scope_t::ref module_scope = dyncast<module_scope_t>(scope);
	if (module_scope == nullptr) {
		throw user_error(get_location(), "link var cannot be used outside of module scope");
	}

	return var_decl->resolve_as_link(builder, module_scope);
}

bound_var_t::ref ast::link_function_statement_t::resolve_expression(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	assert(expected_type == nullptr);
	assert(!as_ref);

	/* FFI */
	module_scope_t::ref module_scope = dyncast<module_scope_t>(scope);
	assert(module_scope);

	types::type_t::ref type_constraints;
	types::type_function_t::ref function_type;
	bound_type_t::named_pairs named_args;
	bound_type_t::ref return_value;
	bool needs_type_fixup = false;
	try {
		destructure_function_decl(builder, *extern_function, scope, type_constraints,
				false /*as_closure*/, needs_type_fixup, named_args, return_value, function_type,
				expected_type);
		assert(!needs_type_fixup);
	} catch (unbound_type_error &error) {
		throw user_error(error.user_error);
	}

	assert(return_value != nullptr);
    if (return_value->is_unit(scope)) {
        throw user_error(token.location, "linked functions cannot return unit type ()");
    }

	bound_type_t::refs args;
	for (auto &named_arg_pair : named_args) {
		args.push_back(named_arg_pair.second);
	}

	llvm::FunctionType *llvm_func_type = llvm_create_function_type(
			builder, args, return_value);

	/* try to find this function, if it already exists... and make sure we use the "link to" name, if specified. */
	llvm::Module *llvm_module = module_scope->get_llvm_module();
	llvm::Value *llvm_value = llvm_module->getOrInsertFunction(extern_function->link_to_name.text, llvm_func_type);

	assert(llvm_print(llvm_value->getType()) != llvm_print(llvm_func_type));

	/* get the full function type */
	types::type_function_t::ref function_sig = get_function_type(type_constraints, args, return_value);
	debug_above(3, log(log_info, "%s has type %s",
				extern_function->get_function_name().c_str(),
				function_sig->str().c_str()));

	/* actually create or find the finalized bound type for this function */
	bound_type_t::ref bound_function_type = upsert_bound_type(
			builder, scope, function_sig);

	return bound_var_t::create(
			INTERNAL_LOC(),
			scope->make_fqn(extern_function->token.text),
			bound_function_type,
			llvm_value,
			make_code_id(extern_function->token));
}

void ast::link_name_t::resolve_statement(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	not_impl();
}

bound_var_t::ref ast::dot_expr_t::resolve_overrides(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		const ptr<const ast::item_t> &callsite,
		const bound_type_t::refs &args,
		types::type_t::ref return_type) const
{
	INDENT(5, string_format(
				"dot_expr_t::resolve_overrides for %s with %s -> %s",
				callsite->str().c_str(),
				::str(args).c_str(),
				return_type != nullptr ? return_type->str().c_str() : c_type("any")));

	/* check the left-hand side first, it should be a type_namespace */
	bound_var_t::ref lhs_var = lhs->resolve_expression(
			builder, scope, life, false /*as_ref*/, nullptr);

	if (auto bound_module = dyncast<const bound_module_t>(lhs_var)) {
		assert(bound_module->module_scope != nullptr);

		/* let's see if the associated module has a method that can handle this callsite */
		return get_callable(builder, bound_module->module_scope,
				rhs.text, callsite->get_location(),
				get_args_type(args),
				return_type);
	} else {
		types::type_function_t::ref target_function_type = get_function_type(
				type_variable(INTERNAL_LOC()),
				args,
				type_variable(INTERNAL_LOC()));
		bound_var_t::ref bound_fn = this->resolve_expression(builder, scope, life, false /*as_ref*/,
				target_function_type);

		// dbg_when(dyncast<const types::type_function_closure_t>(bound_fn->type->get_type()) != nullptr);
		unification_t unification = unify(
				bound_fn->type->get_type(),
				target_function_type,
				scope);

		if (unification.result) {
			return bound_fn;
		} else {
			throw user_error(lhs->get_location(),
					"function %s is not compatible with arguments %s",
					bound_fn->str().c_str(),
					::str(args).c_str());
		}
	}
}

ptr<ast::callsite_expr_t> expand_callsite_string_literal(
		token_t token,
		std::string module,
		std::string function_name,
		std::string param)
{
	param = clean_ansi_escapes(param);
	/* create the function name, which is a fully qualified module.function expression */
	auto dot_expr = ast::create<ast::dot_expr_t>(token);
	dot_expr->lhs = ast::create<ast::reference_expr_t>(token_t{token.location, tk_identifier, module});
	dot_expr->rhs = token_t{token.location, tk_identifier, function_name};

	/* have the dot expr call with the `param` value as its one parameter */
	auto callsite = ast::create<ast::callsite_expr_t>(token);
	callsite->function_expr = dot_expr;
	callsite->params = std::vector<ptr<ast::expression_t>>{
		ast::create<ast::literal_expr_t>(token_t{token.location, tk_string, escape_json_quotes(param)})
	};

	return callsite;
}

void resolve_assert_macro(
		llvm::IRBuilder<> &builder, 
		scope_t::ref scope, 
		life_t::ref life,
		token_t token,
		ptr<ast::expression_t> condition,
		runnable_scope_t::ref *new_scope)
{
	auto if_block = ast::create<ast::if_block_t>(token);
	if_block->condition = condition;

	auto callsite = expand_callsite_string_literal(token, "runtime", "on_assert_failure", 
			string_format("%s: assertion %s failed",
				token.location.str().c_str(),
				condition->str().c_str()));

	auto then_block = ast::create<ast::block_t>(token);
	auto else_block = ast::create<ast::block_t>(token);
	else_block->statements.push_back(callsite);
	else_block->statements.push_back(ast::create<ast::unreachable_t>(token));
	if_block->block = then_block;
	if_block->else_ = else_block;

	bool if_block_returns = false;
	if_block->resolve_statement(
			builder,
			scope,
			life,
			new_scope,
			&if_block_returns);
}

void ast::callsite_expr_t::resolve_statement(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	if (auto symbol = dyncast<ast::reference_expr_t>(function_expr)) {
		if (symbol->token.text == "static_print") {
			if (params.size() == 1) {
				auto param = params[0];
				if (auto ref_expr = dyncast<const ast::reference_expr_t>(param)) {
					if (ref_expr->token.text == "return") {
						/* print the return type of the current function */
						auto runnable_scope = dyncast<runnable_scope_t>(scope);
						assert(runnable_scope != nullptr);
						log_location(log_info, param->get_location(),
								"return type : %s",
								runnable_scope->get_return_type_constraint()->str().c_str());
						return;
					}
				}
				bound_var_t::ref param_var = param->resolve_expression(builder, scope, life, true /*as_ref*/, nullptr);
				log_location(log_info, param->get_location(),
						"%s : %s%s",
						param->str().c_str(),
						param_var->type->str().c_str(),
                        debug_level() >= 8 ? string_format(" %s", scope->get_name().c_str()).c_str() : "");
				return;
			} else {
				throw user_error(get_location(), "static_print requires one and only one parameter");
			}
		} else if (symbol->token.text == "assert") {
			/* do a crude macro expansion here and evaluate that */
			if (params.size() == 1) {
				auto param = params[0];
				resolve_assert_macro(builder, scope, life, symbol->token, param, new_scope);
				return;
			} else {
				throw user_error(get_location(), "assert accepts and requires one parameter");
			}
		}
	}

	resolve_expression(builder, scope, life, false /*as_ref*/, nullptr);
}

bound_var_t::ref ast::callsite_expr_t::resolve_expression(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	try {
		indent_logger indent(get_location(), 5,
			   	string_format("resolving callsite expression of %s with expected type %s",
				str().c_str(), expected_type != nullptr ? expected_type->str().c_str() : "<null>"));

		/* get the value of calling a function */
		bound_type_t::refs param_types;
		bound_var_t::refs arguments;
		types::type_function_t::ref function_type;

		bool need_overload_resolution = false;
		types::type_t::refs args;
		std::vector<llvm::IRBuilderBase::InsertPoint> insertion_points;
		for (size_t j = 0; j < params.size(); ++j) {
			auto param = params[j];
			if (!dyncast<const ast::function_defn_t>(param)) {
				bound_var_t::ref param_var = param->resolve_expression(
						builder, scope, life, false /*as_ref*/, nullptr);

				if (param_var->type->is_void(scope)) {
					throw user_error(param->get_location(), "function parameters cannot be void");
				}

				arguments.push_back(param_var);
				param_types.push_back(param_var->type);
				args.push_back(param_var->type->get_type());
			} else {
				arguments.push_back(nullptr);
				param_types.push_back(nullptr);
				args.push_back(type_variable(INTERNAL_LOC()));
				insertion_points.push_back(builder.saveIP());
				need_overload_resolution = true;
			}
		}

		auto callsite_ip = builder.saveIP();

		if (need_overload_resolution) {
			if (auto can_reference_overloads = dyncast<can_reference_overloads_t>(function_expr)) {
				function_type = can_reference_overloads->resolve_arg_types_from_overrides(
						scope, get_location(),
						args, nullptr);
				/*
				if (function_type == nullptr) {
					throw user_error(get_location(), "could not find a function type for %s with args %s",
							function_expr->str().c_str(),
							::str(args).c_str());
				}
				*/
			} else {
				throw user_error(get_location(), "cannot reference overloads when trying to resolve overloads");
			}

			/* now instantiate the parameter values as per their appropriate expected types, but if we hit an undefined type
			 * error, then try to expand our understanding of the function we're calling, and continue. */
			for (size_t i = 0, j = 0; j < params.size(); ++j) {
				if (arguments[j] == nullptr) {
					auto param = params[j];
					builder.restoreIP(insertion_points[i++]);

					auto expected_type_for_arg = get_arg_from_function(function_type, j);
					debug_above(7, log(log_info, "resolving parameter %d with expected type %s",
								j, expected_type_for_arg ? expected_type_for_arg->str().c_str() : "<null>"));
					bound_var_t::ref param_var = param->resolve_expression(
							builder, scope, life, false /*as_ref*/,
							expected_type_for_arg);

					debug_above(6, log("argument %s -> %s", param->str().c_str(), param_var->type->str().c_str()));

					assert(arguments[j] == nullptr);
					arguments[j] = param_var;
					assert(param_types[j] == nullptr);
					param_types[j] = param_var->type;
					args[j] = param_var->type->get_type();
				}
			}
		}

		builder.restoreIP(callsite_ip);
		if (auto can_reference_overloads = dyncast<can_reference_overloads_t>(function_expr)) {
			/* we need to figure out which overload to call, if there are any */
			debug_above(6, log("arguments to resolve in callsite are %s",
						::str(arguments).c_str()));
			debug_above(6, log("resolving against lhs %s",
						function_expr->str().c_str()));
			bound_var_t::ref function = can_reference_overloads->resolve_overrides(
					builder, scope, life, shared_from_this(),
					bound_type_t::refs_from_vars(arguments),
					expected_type);

			debug_above(5, log(log_info, "function chosen is %s", function->str().c_str()));

			return make_call_value(builder, get_location(), scope, life, function, arguments);
		} else {
			bound_var_t::ref lhs_value = function_expr->resolve_expression(builder, scope, life, false /*as_ref*/,
					type_function_closure(type_variable(INTERNAL_LOC())));
			return make_call_value(builder, get_location(), scope, life, lhs_value, arguments);
		}
	} catch (user_error &e) {
		std::throw_with_nested(user_error(log_info, 
					get_location(), "while resolving callsite expression of %s with expected type %s",
					str().c_str(), expected_type != nullptr ? expected_type->str().c_str() : "<null>"));
	}
}

bound_var_t::ref ast::typeinfo_expr_t::resolve_expression(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	auto bindings = scope->get_type_variable_bindings();
	auto full_type = type->rebind(bindings);

	debug_above(3, log("evaluating typeinfo(%s)", full_type->str().c_str()));

	auto bound_type = upsert_bound_type(builder, scope, full_type);
	types::type_t::ref expanded_type = full_type->eval(scope, true);
	debug_above(3, log("type evaluated to %s", expanded_type->str().c_str()));

	/* destructure the structure that this should have */
	if (auto pointer = dyncast<const types::type_ptr_t>(expanded_type)) {
		if (auto managed = dyncast<const types::type_managed_t>(pointer->element_type)) {
			expanded_type = managed->element_type;
		} else {
			assert(false);
			return null_impl();
		}
	} else if (auto ref = dyncast<const types::type_ref_t>(expanded_type)) {
		// bug in not handling this above?
		assert(false);
	}

	/* at this point we should have a struct type in expanded_type */
	if (auto struct_type = dyncast<const types::type_struct_t>(expanded_type)) {
		bound_type_t::refs args = upsert_bound_types(
				builder, scope, struct_type->dimensions);

		dbg();
		// TODO: find the dtor
		return upsert_type_info(
				builder,
				scope,
				struct_type->repr().c_str(),
				full_type->get_location(),
				bound_type,
				args,
				nullptr,
				nullptr);
	} else if (auto extern_type = dyncast<const types::type_extern_t>(expanded_type)) {
		/* we need this in order to be able to get runtime type information */
		auto program_scope = scope->get_program_scope();
		std::string type_info_var_name = string_format("__type_info_%s",
				extern_type->inner->repr().c_str());
		bound_type_t::ref var_ptr_type = program_scope->get_runtime_type(builder, STD_MANAGED_TYPE, true /*get_ptr*/);
		/* before we go create this type info, let's see if it already exists */
		auto bound_type_info = program_scope->get_bound_variable(builder, full_type->get_location(),
				type_info_var_name);

		if (bound_type_info != nullptr) {
			/* we've already created this bound type info, so let's just return it */
			return bound_type_info;
		}

		/* we have to create it */
		auto bound_underlying_type = upsert_bound_type(builder, scope, underlying_type);

		auto llvm_linked_type = bound_underlying_type->get_llvm_type();
		llvm::Module *llvm_module = llvm_get_module(builder);

#if 0
		llvm::FunctionType *llvm_var_fn_type = llvm::FunctionType::get(
				builder.getVoidTy(),
				llvm::ArrayRef<llvm::Type*>(
					std::vector<llvm::Type*>{var_ptr_type->get_llvm_type()}),
				false /*isVarArg*/);
#endif

		/* get references to the functions named by the user */
		bound_var_t::ref finalize_fn = get_callable(
				builder,
				scope,
				finalize_function.text,
				finalize_function.location,
				type_args({var_ptr_type->get_type()}, {}),
				type_void());

		llvm::Constant *llvm_finalize_fn = llvm::dyn_cast<llvm::Constant>(finalize_fn->get_llvm_value());

		bound_var_t::ref mark_fn = get_callable(
				builder,
				scope,
				mark_function.text,
				mark_function.location,
				type_args({var_ptr_type->get_type()}, {}),
				type_void());
		llvm::Constant *llvm_mark_fn = llvm::dyn_cast<llvm::Constant>(mark_fn->get_llvm_value());

		bound_type_t::ref type_info = program_scope->get_runtime_type(builder, "type_info_t");
		bound_type_t::ref type_info_mark_fn = program_scope->get_runtime_type(builder, "type_info_mark_fn_t");
		llvm::StructType *llvm_type_info_type = llvm::cast<llvm::StructType>(
				type_info->get_llvm_type());

		llvm::Constant *llvm_sizeof_tuple = llvm_sizeof_type(builder, llvm_linked_type);
		auto signature = full_type->get_signature();

		llvm::Constant *llvm_type_info = llvm_create_constant_struct_instance(
				llvm_type_info_type,
				{
					/* the kind of this type_info */
					builder.getInt32(type_kind_use_mark_fn),

					/* allocation size */
					llvm_sizeof_tuple,

					/* name this variable */
					(llvm::Constant *)builder.CreateGlobalStringPtr(type_info_var_name),
				});

		llvm::Constant *llvm_type_info_mark_fn = llvm_create_struct_instance(
				string_format("__type_info_mark_fn_%s", signature.c_str()),
				llvm_module,
				llvm::dyn_cast<llvm::StructType>(type_info_mark_fn->get_llvm_type()),
				{
					/* the type info header */
					llvm_type_info,

					/* finalize_fn */
					llvm_finalize_fn,

					/* mark_fn */
					llvm_mark_fn,
				});

		debug_above(5, log(log_info, "llvm_type_info_mark_fn = %s",
					llvm_print(llvm_type_info_mark_fn).c_str()));

		bound_type_t::ref type_info_ptr_type = program_scope->get_runtime_type(builder, "type_info_t", true /*get_ptr*/);
		auto bound_type_info_var = bound_var_t::create(
				INTERNAL_LOC(),
				type_info_var_name,
				type_info_ptr_type,
				llvm::ConstantExpr::getPointerCast(
					llvm_type_info_mark_fn,
					type_info_ptr_type->get_llvm_type()),
				make_iid("type info value"));

		program_scope->put_bound_variable(
				type_info_var_name,
				bound_type_info_var);
		return bound_type_info_var;
	} else {
		not_impl();
		return nullptr;
	}
}

bound_var_t::ref ast::reference_expr_t::resolve_condition(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		types::type_t::ref expected_type,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false) const
{
	return resolve_reference(builder, scope, life, false /*as_ref*/,
		   	expected_type, scope_if_true, scope_if_false);
}

bound_var_t::ref ast::reference_expr_t::resolve_expression(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	return resolve_reference(builder, scope, life, as_ref, expected_type, nullptr, nullptr);
}

runnable_scope_t::ref new_refined_scope(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		std::string name,
		bound_var_t::ref value,
		bool refinement_path)
{
	/* create a new nested scope with a refined type for the given named value assuming it is truthy
	 * or falsey, according to the value of `refinement_path`. */
	auto local_scope = dyncast<runnable_scope_t>(scope);
	assert(local_scope != nullptr);

	types::type_t::ref value_type = value->type->get_type();
	types::type_t::ref refined_type = value_type->boolean_refinement(!refinement_path, scope);

	if (refined_type != value_type) {
		bound_type_t::ref bound_refined_type = upsert_bound_type(builder, scope, refined_type);

		auto new_scope = local_scope->new_runnable_scope(
				string_format("%s.%s", boolstr(refinement_path), name.c_str()));
		new_scope->put_bound_variable(name,
				bound_var_t::create(
					INTERNAL_LOC(),
					name,
					bound_refined_type,
					value->get_llvm_value(),
					make_iid_impl(name, location)));
		return new_scope;
	} else {
		/* no new scope needed */
		return nullptr;
	}
}

bound_var_t::ref ast::reference_expr_t::resolve_reference(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false) const
{
	/* we wouldn't be referencing a variable name here unless it was unique
	 * override resolution only happens on callsites, and we don't allow
	 * passing around unresolved overload references */
	bound_var_t::ref var = scope->get_bound_variable(builder, get_location(), token.text);

	/* get_bound_variable can return nullptr without an user_error */
	if (var != nullptr) {
		if (!as_ref) {
			bound_var_t::ref value = var->resolve_bound_value(builder, scope);
			if (scope_if_true != nullptr && scope_if_false != nullptr && value->type->is_maybe(scope)) {
				assert(*scope_if_true == nullptr);
				assert(*scope_if_false == nullptr);
				*scope_if_true = new_refined_scope(builder, scope, token.location, token.text, value, true);
				*scope_if_false = new_refined_scope(builder, scope, token.location, token.text, value, false);
			}
			return value;
		} else {
			assert(scope_if_true == nullptr && scope_if_false == nullptr);
			return var;
		}
	} else if (auto function_type = dyncast<const types::type_function_t>(expected_type)) {
		indent_logger indent(get_location(), 5, string_format("looking for reference_expr " c_id("%s"),
					token.text.c_str()));
		var_t::refs fns;
		fittings_t fittings;
		auto function = maybe_get_callable(builder, scope, token.text,
				get_location(), function_type->args, function_type->return_type, fns, fittings);
		if (function != nullptr) {
			debug_above(5, log("reference expression for " c_id("%s") " resolved to %s",
						token.text.c_str(), function->str().c_str()));
			assert(function->type->get_type()->eval_predicate(tb_function, scope));
			return function;
		} else {
			debug_above(5, log("could not find reference expression for " c_id("%s") " (found %d fns, though)",
						token.text.c_str(), fittings.size()));
		}
	} else {
		unchecked_var_t::ref unchecked_fn = scope->get_module_scope()->get_unchecked_variable(token.text);
		if (unchecked_fn != nullptr) {
			types::type_function_t::ref fn_type = dyncast<const types::type_function_t>(unchecked_fn->get_type(scope)->rebind(scope->get_type_variable_bindings())->eval(scope));
			if (fn_type != nullptr) {
				return instantiate_unchecked_fn(
						builder,
						scope,
						unchecked_fn,
						fn_type,
						{});
			} else {
				throw user_error(get_location(), "unable to instantiate unchecked function %s",
						unchecked_fn->str().c_str());
			}
		}
	}

	throw user_error(get_location(), "undefined symbol " c_id("%s"), token.text.c_str());
}

bound_var_t::ref ast::array_index_expr_t::resolve_expression(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	return resolve_assignment(builder, scope, life, as_ref, nullptr, expected_type);
}

bound_var_t::ref resolve_pointer_array_index(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		types::type_t::ref element_type,
		ast::expression_t::ref index,
		bound_var_t::ref index_val,
		ast::expression_t::ref lhs,
		bound_var_t::ref lhs_val,
		bool as_ref,
		ast::expression_t::ref rhs)
{
	/* this is a native pointer - aka an array in memory */
	assert(!dyncast<const types::type_managed_t>(element_type));
	debug_above(5, log("__getitem__ found that we are looking for items of type %s",
				element_type->str().c_str()));

	// REVIEW: consider just checking the LLVM type for whether it's an integer type
	unification_t index_unification = unify(
			type_integer(type_variable(INTERNAL_LOC()), type_variable(INTERNAL_LOC())),
			index_val->type->get_type(),
			scope);

	if (index_unification.result) {
		debug_above(5, log(log_info,
					"dereferencing %s[%s] with a GEP",
					lhs->str().c_str(), 
					index_val->str().c_str()));

		/* create the GEP instruction */
		std::vector<llvm::Value *> gep_path = std::vector<llvm::Value *>{index_val->get_llvm_value()};

		llvm::Value *llvm_gep = builder.CreateGEP(lhs_val->get_llvm_value(), gep_path);

		debug_above(5, log(log_info,
					"created dereferencing GEP %s : %s (element type is %s)",
					llvm_print(*llvm_gep).c_str(),
					llvm_print(llvm_gep->getType()).c_str(),
					element_type->str().c_str()));

		if (rhs == nullptr) {
			/* get the element type (taking as_ref into consideration) */
			bound_type_t::ref bound_element_type = upsert_bound_type(
					builder, scope,
					as_ref ? type_ref(element_type) : element_type);

			return bound_var_t::create(
					INTERNAL_LOC(),
					{"dereferenced.pointer"},
					bound_element_type,
					as_ref ? llvm_gep : builder.CreateLoad(llvm_gep),
					make_iid_impl("dereferenced.pointer", lhs_val->get_location()));
		} else {
			/* we are assigning to a native pointer dereference */
			auto value = rhs->resolve_expression(builder, scope, life, false /*as_ref*/, element_type);
			llvm::Value *llvm_value = coerce_value(builder, scope, life, lhs->get_location(), element_type, value);
			builder.CreateStore(llvm_value, llvm_gep);
			return nullptr;
		}
	} else {
		throw user_error(index->get_location(),
				"pointer index must be of an integer type. your index is of type %s",
				index_val->type->get_type()->str().c_str());
	}
}

types::type_args_t::ref get_function_args_types(bound_type_t::ref function_type) {
	if (auto type_function = dyncast<const types::type_function_t>(function_type->get_type())) {
		return dyncast<const types::type_args_t>(type_function->args);
	}

	assert(false);
	return nullptr;
}

types::type_struct_t::ref get_struct_type_from_bound_type(
		scope_t::ref scope,
		location_t location,
		bound_type_t::ref bound_type)
{
	auto type = bound_type->get_type()->eval(scope, true);

	if (auto maybe_type = dyncast<const types::type_maybe_t>(type)) {
		throw user_error(location, "maybe types cannot be dereferenced. try checking whether it's not equal to null first");
		return nullptr;
	} else if (auto tuple_type = dyncast<const types::type_tuple_t>(type)) {
		return type_struct(tuple_type->dimensions, {});
	} else if (auto ptr_type = dyncast<const types::type_ptr_t>(type)) {
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

	throw user_error(location,
			"could not find any member variables within %s (%s)",
			bound_type->str().c_str(),
			type->str().c_str());
}

bound_var_t::ref extract_member_by_index(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		bound_var_t::ref bound_var,
		bound_type_t::ref bound_obj_type,
		int index,
		std::string member_name,
		bool as_ref)
{
	types::type_struct_t::ref struct_type = get_struct_type_from_bound_type(
			scope, location, bound_obj_type);

	if (index < 0 || index >= (int)struct_type->dimensions.size()) {
		throw user_error(location, "tuple index is out of bounds. tuple %s has %d elements",
				struct_type->str().c_str(), (int)struct_type->dimensions.size());
	}

	/* get an GEP-able version of the object */
	llvm::Value *llvm_var_value = llvm_maybe_pointer_cast(builder,
			bound_var->resolve_bound_var_value(scope, builder),
			bound_obj_type->get_llvm_specific_type());

	/* the following code is heavily coupled to the physical layout of
	 * managed vs. native structures */

	/* GEP and load the member value from the structure */
	llvm::Value *llvm_gep = llvm_make_gep(builder,
			llvm_var_value, index,
			types::is_managed_ptr(bound_var->get_type(), scope));
	if (llvm_gep->getName().str().size() == 0) {
		llvm_gep->setName(string_format("address_of.%s", member_name.c_str()));
	}

	/* check whether this member_type is allowed to be returned as a ref or not */
	auto member_type = struct_type->dimensions[index];
	llvm::Value *llvm_item = (
			(as_ref && member_type->eval_predicate(tb_ref, scope))
			? llvm_gep
			: builder.CreateLoad(llvm_gep));

	if (llvm_item->getName().str().size() == 0) {
		/* add a helpful descriptive name to this local value */
		auto value_name = string_format(".%s", member_name.c_str());
		llvm_item->setName(value_name);
	}

	/* get the type of the dimension being referenced */
	bound_type_t::ref bound_member_type = upsert_bound_type(builder, scope, 
			as_ref ? member_type : types::without_ref(member_type));

	auto dot_name = string_format("%s.%s", bound_var->name.c_str(), member_name.c_str());
	return bound_var_t::create(
			INTERNAL_LOC(), dot_name,
			bound_member_type, llvm_item, make_iid_impl(dot_name, location));
}

int64_t parse_int_value(token_t token) {
	switch (token.tk) {
	case tk_integer:
		{
			int64_t value;
			if (token.text.size() > 2 && token.text.substr(0, 2) == "0x") {
				value = strtoll(token.text.substr(2).c_str(), nullptr, 16);
			} else {
				value = atoll(token.text.c_str());
			}
			return value;
		}
		break;
	default:
		throw user_error(token.location, "unable to read an integer value from %s", token.str().c_str());
	}
}

int get_constant_int(ast::item_t::ref item) {
	return parse_int_value(item->token);
}

bound_var_t::ref type_check_assignment(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bound_var_t::ref lhs_var,
		bound_var_t::ref rhs_var,
		location_t location)
{
	if (!lhs_var->type->is_ref(scope)) {
		auto error = user_error(location,
				"the left-hand side of this assignment is not a reference",
				lhs_var->name.c_str());
		error.add_info(lhs_var->get_location(),
				"see declaration of " c_id("%s") " with type %s",
				lhs_var->name.c_str(),
				lhs_var->type->get_type()->str().c_str());
		throw error;
	}

	INDENT(5, string_format(
				"type checking assignment %s = %s",
				lhs_var->str().c_str(),
				rhs_var->str().c_str()));

	auto lhs_unreferenced_type = dyncast<const types::type_ref_t>(lhs_var->type->get_type())->element_type;
	bound_type_t::ref lhs_unreferenced_bound_type = upsert_bound_type(builder, scope, lhs_unreferenced_type);

	unification_t unification = unify(
			lhs_unreferenced_type,
			rhs_var->type->get_type(),
			scope);

	if (unification.result) {
		llvm::Value *llvm_rhs_value = coerce_value(builder, scope, life,
				location, lhs_unreferenced_type, rhs_var);
		assert(llvm::dyn_cast<llvm::AllocaInst>(lhs_var->get_llvm_value())
				|| llvm::dyn_cast<llvm::GlobalVariable>(lhs_var->get_llvm_value())
				|| llvm_value_is_pointer(lhs_var->get_llvm_value()));

		builder.CreateStore(llvm_rhs_value, lhs_var->get_llvm_value());

		return lhs_var;
	} else {
		throw user_error(location, "left-hand side is incompatible with the right-hand side (%s)",
				unification.str().c_str());
	}
}

bound_var_t::ref ast::array_index_expr_t::resolve_assignment(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		const ast::expression_t::ref &rhs,
		types::type_t::ref expected_type) const
{
	/* this expression looks like this (the rhs is optional)
	 *
	 *   lhs[index] = rhs
	 *
	 */

	if (rhs != nullptr) {
		/* make sure to treat the array dereference as a reference if we are doing an assignment */
		as_ref = true;
	}

	bound_var_t::ref lhs_val = lhs->resolve_expression(builder,
			scope, life, false /*as_ref*/, nullptr);
	if (auto tuple_type = dyncast<const types::type_tuple_t>(lhs_val->type->get_type())) {
		int member_index = get_constant_int(start);
		if (stop != nullptr) {
			throw user_error(stop->get_location(), "slicing tuples is not yet supported. accepting pull requests...");
		}

		bound_var_t::ref value = extract_member_by_index(builder, scope, life,
				get_location(), lhs_val, lhs_val->type, member_index,
				string_format("%d", member_index), as_ref);

		if (rhs != nullptr) {
			/* let's assign into this tuple slot */
			bound_var_t::ref rhs_val = rhs->resolve_expression(builder, scope, life, false /*as_ref*/, value->type->get_type());

			/* we shouldn't have known what type to expect, because this is a
			 * statement */
			assert(expected_type == nullptr);
			type_check_assignment(builder, scope, life, value,
					rhs_val, token.location);
			return nullptr;
		} else {
			return value;
		}
	} else {
		// REVIEW: might want to move this after evaluation of rhs, if rhs exists
		bound_var_t::ref index_val = start->resolve_expression(builder, scope, life, false /*as_ref*/, nullptr);

		identifier::ref element_type_var = types::gensym(lhs_val->type->get_location());

		if (lhs_val->type->get_type()->eval_predicate(tb_maybe, scope)) {
			throw user_error(lhs_val->get_location(), "you are not allowed to dereference a potentially null pointer");
		}

		/* check to see if we are employing pointer arithmetic here */
		unification_t unification = unify(
				lhs_val->type->get_type(),
			   	type_ptr(type_variable(element_type_var)),
				scope);

		if (unification.result) {
			if (stop != nullptr) {
				throw user_error(stop->get_location(), "native pointer arrays cannot be sliced");
			}

			/* this is a native pointer, let's generate code to write or read, or reference it */
			types::type_t::ref element_type = unification.bindings[element_type_var->get_name()];
			return resolve_pointer_array_index(builder, scope, life, element_type, start, index_val,
					lhs, lhs_val, as_ref, rhs);
		} else
			if (rhs == nullptr) {
				bound_var_t::ref stop_val = (
						(stop != nullptr)
						? stop->resolve_expression(builder, scope, life, false /*as_ref*/, nullptr)
						: nullptr);

				/* this is not a native pointer we are dereferencing */
				debug_above(5, log("attempting to call " c_id("%s") " on %s and %s",
							stop != nullptr ? "__getslice__" : "__getitem__",
							lhs_val->str().c_str(), index_val->str().c_str()));

				if (stop_val != nullptr) {
					/* get or instantiate a function we can call on these arguments */
					return call_module_function(builder, scope, life,
							"__getslice__", get_location(), {lhs_val, index_val, stop_val});
				} else {
					/* get or instantiate a function we can call on these arguments */
					return call_module_function(builder, scope, life,
							"__getitem__", get_location(), {lhs_val, index_val});
				}
			} else {
				/* we're assigning to a managed array index expression */
				if (stop != nullptr) {
					throw user_error(stop->get_location(), "assigning to a slice is not yet supported. accepting pull requests...");
				}

				/* let's first try to find the setitem function while using a free-type
				 * variable for the rhs parameter. */
				auto type_var_name = types::gensym(INTERNAL_LOC());
				var_t::refs fns;
				fittings_t fittings;
				bound_var_t::ref setitem_function = maybe_get_callable(
						builder,
						scope,
						"__setitem__",
						get_location(),
						type_args({
							lhs_val->type->get_type(),
						   	index_val->type->get_type(),
						   	type_variable(type_var_name)}),
						type_variable(INTERNAL_LOC()),
						fns,
						fittings);

				debug_above(9, log("resolved setitem to %s", setitem_function->str().c_str()));

				types::type_t::ref expected_rhs_type;
				if (setitem_function != nullptr) {
					if (auto function = dyncast<const types::type_function_t>(setitem_function->type->get_type())) {
						if (auto args = dyncast<const types::type_args_t>(function->args)) {
							assert(args->args.size() == 3);
							/* we found the expected rhs type */
							expected_rhs_type = args->args[2];
						}
					}
				}

				if (expected_rhs_type == nullptr) {
					throw user_error(get_location(), "unable to figure out the expected type of the right-hand side");
				}

				/* let's solve for the rhs */
				bound_var_t::ref rhs_val = rhs->resolve_expression(builder, scope, life, false /*as_ref*/,
						expected_rhs_type);

				if (!unifies(expected_rhs_type, rhs_val->type->get_type(), scope)) {
					auto error = user_error(rhs->get_location(), "incompatible rhs for assignment");
					error.add_info(setitem_function->get_location(), "see definition of __setitem__");
					error.add_info(expected_rhs_type->get_location(), "it is expecting a value of type %s",
							expected_rhs_type->str().c_str());
					error.add_info(rhs_val->get_location(), "and your rhs is of type %s", rhs_val->type->get_type()->str().c_str());
					throw error;
				}

				/* we have a rhs to assign into this lhs, let's find the function we should be calling to do
				 * the update. */
				std::vector<llvm::Value *> llvm_args = get_llvm_values(
						builder, scope, life,
						get_location(),
						get_function_args_types(setitem_function->type),
						{lhs_val, index_val, rhs_val});

				bound_type_t::ref return_type = get_function_return_type(
						builder,
						scope,
						setitem_function->type);
				return bound_var_t::create(
						INTERNAL_LOC(),
						"array.index.assignment",
						return_type,
						llvm_create_call_inst(
							builder, lhs->get_location(),
							setitem_function,
							llvm_args),
						make_iid("array.index.assignment"));
			}
	}
}

bound_var_t::ref create_bound_vector_literal(
		
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		types::type_t::ref element_type,
		bound_var_t::refs bound_items)
{
	debug_above(5, log("creating a vector literal with element type %s and items %s",
				element_type->str().c_str(),
				::str(bound_items).c_str()));

	auto program_scope = scope->get_program_scope();

	auto bound_var_ptr_type = program_scope->get_runtime_type(builder, STD_MANAGED_TYPE, true /*get_ptr*/);

	auto bound_var_ptr_ptr_type = upsert_bound_type(
			builder, scope,
			type_ptr(bound_var_ptr_type->get_type()));

	/* create the type for this vector */
	types::type_t::ref vector_type = type_operator(
			type_id(make_iid_impl(STD_VECTOR_TYPE, location)),
			element_type);
	bound_type_t::ref bound_vector_type = upsert_bound_type(
			builder, scope, vector_type);

	/* get the function to allocate a vector and reserve enough space */
	bound_var_t::ref get_vector_init_function = get_callable(
			builder,
			scope,
			"vector.__init_vector__",
			location,
			type_args({type_id(make_iid("size_t"))}),
			vector_type);

	types::type_t::ref vector_impl_type = type_ptr(type_operator(type_id(make_iid_impl("vector.VectorImpl", location)), element_type));

	/* get the raw pointer type to vectors */
	bound_type_t::ref bound_base_vector_type = upsert_bound_type(builder, scope, vector_impl_type);
	debug_above(8, log("bound base vector type for element %s is %s",
				element_type->str().c_str(),
				bound_base_vector_type->str().c_str()));

	/* get the append function for vectors */
	bound_var_t::ref get_vector_append_function = get_callable(
			builder,
			scope,
			"vector.__vector_unsafe_append__",
			location,
			type_args({vector_impl_type, element_type}),
			type_id(make_iid("void")));

	/* get a new vector of the given size */
	llvm::CallInst *llvm_vector = llvm_create_call_inst(
			builder, location, get_vector_init_function,
			{builder.getZionInt(bound_items.size())});

	auto append_fn_type = dyncast<const types::type_function_t>(get_vector_append_function->type->get_type());
	auto element_args_type = dyncast<const types::type_args_t>(append_fn_type->args);
	auto arg0_type = element_args_type->args[0];
	auto arg1_type = element_args_type->args[1];
	auto raw_vector_type = upsert_bound_type(builder, scope, arg0_type);

	llvm::Value *llvm_raw_vector = llvm_maybe_pointer_cast(builder, llvm_vector, raw_vector_type->get_llvm_type());

	/* append all of the items */
	for (auto bound_item : bound_items) {
		llvm::Value *llvm_value = coerce_value(
				builder, scope, life,
				bound_item->get_location(),
				arg1_type,
				bound_item);

		/* call the append function */
		llvm_create_call_inst(builder, bound_item->get_location(),
				get_vector_append_function, {llvm_raw_vector, llvm_value});
	}

	/* the type of the resultant vector */
	return bound_var_t::create(
			INTERNAL_LOC(),
			"vector.literal",
			bound_vector_type,
			llvm_vector,
			make_iid_impl("vector.literal", location));
}

bound_var_t::ref ast::array_literal_expr_t::resolve_expression(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool /*as_ref*/,
		types::type_t::ref expected_type) const
{
	types::type_t::ref expected_element_type;
	types::type_t::ref element_type;

	if (expected_type != nullptr) {
		auto type_var_name = types::gensym(token.location);
		unification_t unification = unify(
				type_operator(type_id(make_iid(STD_VECTOR_TYPE)), type_variable(type_var_name)),
				expected_type,
				scope);

		if (unification.result) {
			expected_element_type = unification.bindings[type_var_name->get_name()];
			if (expected_element_type != nullptr) {
				element_type = expected_element_type;
			}
		}
	}

	bound_var_t::refs bound_items;
	for (auto item : items) {
		auto bound_item = item->resolve_expression(builder, scope, life, false /*as_ref*/,
				expected_element_type);

		bound_items.push_back(bound_item);
		if (element_type == nullptr) {
			element_type = bound_item->type->get_type();
		} else {

			if (!unifies(element_type, bound_item->type->get_type(), scope)) {
				auto error = user_error(bound_item->get_location(), "vector item is incompatible with container type");
				if (dyncast<const types::type_lambda_t>(element_type)) {
					error.add_info(element_type->get_location(), "you may be missing an application of a type operator");
				}
				error.add_info(element_type->get_location(), "container is a %s", element_type->str().c_str());
				error.add_info(bound_item->get_location(), "item is a %s", bound_item->type->get_type()->str().c_str());
				throw error;
			}
		}
	}

	if (items.size() == 0 && element_type == nullptr) {
		throw user_error(get_location(), "not enough information to infer the element type for the vector literal (expected type is %s)",
				expected_type != nullptr ? expected_type->str().c_str() : "<unknown>");
	}

	debug_above(6, log("creating vector literal of type %s", element_type->str().c_str()));
	return create_bound_vector_literal(
			builder, scope, life,
			get_location(), element_type, bound_items);
}

enum rnpbc_t {
	rnpbc_eq,
	rnpbc_ineq,
};

bool rnpbc_equality_is_truth(rnpbc_t rnpbc) {
	switch (rnpbc) {
	case rnpbc_eq:
		return true;
	case rnpbc_ineq:
		return true;
	}
	panic("unreachable rnpbc_equality_is_truth");
	return false;
}

bool rnpbc_rhs_non_null_is_truth(rnpbc_t rnpbc) {
	switch (rnpbc) {
	case rnpbc_eq:
		return false;
	case rnpbc_ineq:
		return true;
	}
	panic("unreachable rnpbc_rhs_non_null_is_truth");
	return false;
}

bool rnpbc_lhs_non_null_is_truth(rnpbc_t rnpbc) {
	switch (rnpbc) {
	case rnpbc_eq:
		return false;
	case rnpbc_ineq:
		return true;
	}
	panic("unreachable rnpbc_lhs_non_null_is_truth");
	return false;
}

bound_var_t::ref resolve_native_pointer_binary_compare(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		location_t location,
		ast::expression_t::ref lhs_node,
		bound_var_t::ref lhs_var,
		ast::expression_t::ref rhs_node,
		bound_var_t::ref rhs_var,
		rnpbc_t rnpbc,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false,
		types::type_t::ref expected_type)
{
	if (lhs_var->type->get_type()->eval_predicate(tb_null, scope)) {
		if (rhs_var->type->get_type()->eval_predicate(tb_null, scope)) {
			return scope->get_program_scope()->get_bound_variable(
					builder, location,
					rnpbc_equality_is_truth(rnpbc) ? TRUE_TYPE : FALSE_TYPE,
					nullptr);
		} else {
			auto null_check = rnpbc_rhs_non_null_is_truth(rnpbc) ? nck_is_non_null : nck_is_null;
			return resolve_null_check(builder, scope, life, location, rhs_node, rhs_var, null_check, scope_if_true, scope_if_false);
		}
	} else if (rhs_var->type->get_type()->eval_predicate(tb_null, scope)) {
		auto null_check = rnpbc_lhs_non_null_is_truth(rnpbc) ? nck_is_non_null : nck_is_null;
		return resolve_null_check(builder, scope, life, location, lhs_node, lhs_var, null_check, scope_if_true, scope_if_false);
	} else {
		/* neither side is null */
		if (!lhs_var->type->is_ptr(scope)) { std::cerr << lhs_var->str() << " " << llvm_print(lhs_var->get_llvm_value()) << std::endl; dbg(); }
		if (!rhs_var->type->is_ptr(scope)) { std::cerr << rhs_var->str() << " " << llvm_print(rhs_var->get_llvm_value()) << std::endl; dbg(); }

		if (
				!unifies(lhs_var->type->get_type(), rhs_var->type->get_type(), scope) &&
				!unifies(rhs_var->type->get_type(), lhs_var->type->get_type(), scope))
		{
			throw user_error(location, "values of types (%s and %s) cannot be compared",
					lhs_var->type->get_type()->str().c_str(),
					rhs_var->type->get_type()->str().c_str());
			return nullptr;
		}

		auto program_scope = scope->get_program_scope();
		llvm::Type *llvm_char_ptr_type = builder.getInt8Ty()->getPointerTo();

		llvm::Value *llvm_value;
		switch (rnpbc) {
		case rnpbc_eq:
			llvm_value = builder.CreateICmpEQ(
					builder.CreateBitCast(lhs_var->get_llvm_value(), llvm_char_ptr_type),
					builder.CreateBitCast(rhs_var->get_llvm_value(), llvm_char_ptr_type));
			break;
		case rnpbc_ineq:
			llvm_value = builder.CreateICmpNE(
					builder.CreateBitCast(lhs_var->get_llvm_value(), llvm_char_ptr_type),
					builder.CreateBitCast(rhs_var->get_llvm_value(), llvm_char_ptr_type));
			break;
		}

		auto bool_type = program_scope->get_bound_type(BOOL_TYPE);
		assert_implies(expected_type != nullptr, unifies(expected_type, bool_type->get_type(), scope));

		return bound_var_t::create(
				INTERNAL_LOC(),
				{"equality.cond"},
				bool_type,
				builder.CreateSExtOrTrunc(llvm_value, bool_type->get_llvm_specific_type()),
				make_iid_impl(std::string{"equality.cond"}, location));
	}
}

bound_var_t::ref resolve_native_pointer_binary_operation(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		location_t location,
		ast::expression_t::ref lhs_node,
		bound_var_t::ref lhs_var,
		ast::expression_t::ref rhs_node,
		bound_var_t::ref rhs_var,
		std::string function_name,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false,
		types::type_t::ref expected_type)
{
	if (function_name == "__binary_eq__" || function_name == "__eq__") {
		return resolve_native_pointer_binary_compare(
				builder, scope, life, location, lhs_node, lhs_var, rhs_node, rhs_var, rnpbc_eq, scope_if_true, scope_if_false, expected_type);
	} else if (function_name == "__binary_ineq__" || function_name == "__ineq__") {
		return resolve_native_pointer_binary_compare(
				builder, scope, life, location, lhs_node, lhs_var, rhs_node, rhs_var, rnpbc_ineq, scope_if_true, scope_if_false, expected_type);
	} else {
		return call_module_function(
				builder, scope, life, function_name,
				location, {lhs_var, rhs_var});
	}
}

bound_var_t::ref type_check_binary_integer_op(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		bound_var_t::ref lhs,
		bound_var_t::ref rhs,
		std::string function_name,
		types::type_t::ref expected_type)
{
	if (expected_type != nullptr) {
		debug_above(8, log("binary integer op is expecting a %s", expected_type->str().c_str()));
	}
	static unsigned int_bit_size = DEFAULT_INT_BITSIZE;
	static bool int_signed = true;
	static bool initialized = false;

	bound_type_t::ref bound_int_type = upsert_bound_type(builder, scope, type_id(make_iid(INT_TYPE)));

	if (!initialized) {
		assert(types::maybe_get_integer_attributes(INTERNAL_LOC(), bound_int_type->get_type(), scope, int_bit_size, int_signed));
		initialized = true;
	}

	unsigned lhs_bit_size, rhs_bit_size;
	bool lhs_signed = false, rhs_signed = false;
	assert(types::maybe_get_integer_attributes(lhs->get_location(), lhs->type->get_type(), scope, lhs_bit_size, lhs_signed));
	assert(types::maybe_get_integer_attributes(rhs->get_location(), rhs->type->get_type(), scope, rhs_bit_size, rhs_signed));

	bound_type_t::ref final_integer_type;
	bool final_integer_signed = false;
	if (lhs_bit_size == rhs_bit_size) {
		if (!lhs_signed == !rhs_signed) {
			final_integer_signed = lhs_signed;
			final_integer_type = upsert_bound_type(builder, scope,
					type_integer(
						type_literal(token_t(location, tk_integer, string_format("%d", lhs_bit_size))),
						type_id(make_iid(string_format("%s", boolstr(lhs_signed))))));
		} else {
			final_integer_signed = true;
			final_integer_type = upsert_bound_type(builder, scope,
					type_integer(
						type_literal(token_t(location, tk_integer, string_format("%d", lhs_bit_size))),
						type_id(make_iid("true"))));
		}
	} else {
		final_integer_signed = true;
		final_integer_type = upsert_bound_type(builder, scope,
				type_integer(
					type_literal(token_t(location, tk_integer, string_format("%d", lhs_bit_size))),
					type_id(make_iid("false"))));
	}

	llvm::Value *llvm_lhs = lhs->get_llvm_value();
	llvm::Value *llvm_rhs = rhs->get_llvm_value();
	assert(llvm_lhs->getType()->isIntegerTy());
	assert(llvm_rhs->getType()->isIntegerTy());

#ifdef ZION_DEBUG
	dump_llir(scope->get_program_scope()->get_llvm_module(), "assert.llir");
	auto llvm_lhs_type = llvm::dyn_cast<llvm::IntegerType>(llvm_lhs->getType());
	assert(llvm_lhs_type != nullptr);
	assert(llvm_lhs_type->getBitWidth() == lhs_bit_size);
	auto llvm_rhs_type = llvm::dyn_cast<llvm::IntegerType>(llvm_rhs->getType());
	assert(llvm_rhs_type != nullptr);
	assert(llvm_rhs_type->getBitWidth() == rhs_bit_size);
#endif

	unsigned computation_bit_size = std::max(std::max(lhs_bit_size, rhs_bit_size), int_bit_size);
	if (lhs_bit_size < computation_bit_size) {
		if (lhs_signed) {
			llvm_lhs = builder.CreateSExtOrTrunc(llvm_lhs, builder.getIntNTy(computation_bit_size));
		} else {
			llvm_lhs = builder.CreateZExtOrTrunc(llvm_lhs, builder.getIntNTy(computation_bit_size));
		}
	}
	if (lhs_bit_size < computation_bit_size) {
		if (lhs_signed) {
			llvm_lhs = builder.CreateSExtOrTrunc(llvm_lhs, builder.getIntNTy(computation_bit_size));
		} else {
			llvm_lhs = builder.CreateZExtOrTrunc(llvm_lhs, builder.getIntNTy(computation_bit_size));
		}
	}
	if (rhs_bit_size < computation_bit_size) {
		if (rhs_signed) {
			llvm_rhs = builder.CreateSExtOrTrunc(llvm_rhs, builder.getIntNTy(computation_bit_size));
		} else {
			llvm_rhs = builder.CreateZExtOrTrunc(llvm_rhs, builder.getIntNTy(computation_bit_size));
		}
	}

	bound_type_t::ref bound_bool_type = upsert_bound_type(builder, scope, type_id(make_iid(BOOL_TYPE)));
	assert(bound_bool_type != nullptr);

	llvm::Value *llvm_value = nullptr;
	if (function_name == "__plus__") {
		llvm_value = builder.CreateAdd(llvm_lhs, llvm_rhs);
	} else if (function_name == "__minus__") {
		llvm_value = builder.CreateSub(llvm_lhs, llvm_rhs);
	} else if (function_name == "__times__") {
		llvm_value = builder.CreateMul(llvm_lhs, llvm_rhs);
	} else if (function_name == "__mod__") {
		if (final_integer_signed) {
			llvm_value = builder.CreateSRem(llvm_lhs, llvm_rhs);
		} else {
			llvm_value = builder.CreateURem(llvm_lhs, llvm_rhs);
		}
	} else if (function_name == "__divide__") {
		if (final_integer_signed) {
			llvm_value = builder.CreateSDiv(llvm_lhs, llvm_rhs);
		} else {
			llvm_value = builder.CreateUDiv(llvm_lhs, llvm_rhs);
		}
	} else if (function_name == "__bitwise_and__") {
		llvm_value = builder.CreateAnd(llvm_lhs, llvm_rhs);
	} else if (function_name == "__bitwise_or__") {
		llvm_value = builder.CreateOr(llvm_lhs, llvm_rhs);
	} else if (function_name == "__xor__") {
		llvm_value = builder.CreateXor(llvm_lhs, llvm_rhs);
	} else if (function_name == "__lt__") {
		return bound_var_t::create(
				INTERNAL_LOC(),
				function_name + ".value",
				bound_bool_type,
				builder.CreateZExtOrTrunc(
					final_integer_signed
					? builder.CreateICmpSLT(llvm_lhs, llvm_rhs)
					: builder.CreateICmpULT(llvm_lhs, llvm_rhs),
					bound_bool_type->get_llvm_type()),
				make_iid(function_name + ".value"));
	} else if (function_name == "__lte__") {
		return bound_var_t::create(
				INTERNAL_LOC(),
				function_name + ".value",
				bound_bool_type,
				builder.CreateZExtOrTrunc(
					final_integer_signed
					? builder.CreateICmpSLE(llvm_lhs, llvm_rhs)
					: builder.CreateICmpULE(llvm_lhs, llvm_rhs),
					bound_bool_type->get_llvm_type()),
				make_iid(function_name + ".value"));
	} else if (function_name == "__gt__") {
		return bound_var_t::create(
				INTERNAL_LOC(),
				function_name + ".value",
				bound_bool_type,
				builder.CreateZExtOrTrunc(
					final_integer_signed
					? builder.CreateICmpSGT(llvm_lhs, llvm_rhs)
					: builder.CreateICmpUGT(llvm_lhs, llvm_rhs),
					bound_bool_type->get_llvm_type()),
				make_iid(function_name + ".value"));
	} else if (function_name == "__gte__") {
		return bound_var_t::create(
				INTERNAL_LOC(),
				function_name + ".value",
				bound_bool_type,
				builder.CreateZExtOrTrunc(
					final_integer_signed
					? builder.CreateICmpSGE(llvm_lhs, llvm_rhs)
					: builder.CreateICmpUGE(llvm_lhs, llvm_rhs),
					bound_bool_type->get_llvm_type()),
				make_iid(function_name + ".value"));
	} else if (function_name == "__ineq__") {
		return bound_var_t::create(
				INTERNAL_LOC(),
				function_name + ".value",
				bound_bool_type,
				builder.CreateZExtOrTrunc(builder.CreateICmpNE(llvm_lhs, llvm_rhs), bound_bool_type->get_llvm_type()),
				make_iid(function_name + ".value"));
	} else if (function_name == "__eq__") {
		return bound_var_t::create(
				INTERNAL_LOC(),
				function_name + ".value",
				bound_bool_type,
				builder.CreateZExtOrTrunc(builder.CreateICmpEQ(llvm_lhs, llvm_rhs), bound_bool_type->get_llvm_type()),
				make_iid(function_name + ".value"));
	} else if (function_name == "__shr__") {
		if (lhs_signed) {
			return bound_var_t::create(
					INTERNAL_LOC(),
					function_name + ".value",
					lhs->type,
					builder.CreateAShr(llvm_lhs, llvm_rhs),
					make_iid(function_name + ".value"));
		} else {
			return bound_var_t::create(
					INTERNAL_LOC(),
					function_name + ".value",
					lhs->type,
					builder.CreateLShr(llvm_lhs, llvm_rhs),
					make_iid(function_name + ".value"));
		}
	} else if (function_name == "__shl__") {
		return bound_var_t::create(
				INTERNAL_LOC(),
				function_name + ".value",
				lhs->type,
				builder.CreateShl(llvm_lhs, llvm_rhs),
				make_iid(function_name + ".value"));
	} else {
		assert(false);
	}

	return bound_var_t::create(
			INTERNAL_LOC(),
			function_name + ".value",
			final_integer_type,
			final_integer_signed
			? builder.CreateSExtOrTrunc(llvm_value, final_integer_type->get_llvm_type())
			: builder.CreateZExtOrTrunc(llvm_value, final_integer_type->get_llvm_type()),
			make_iid_impl(function_name + ".value", location));
}

bound_var_t::ref type_check_binary_operator(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		ast::expression_t::ref lhs_node,
		bound_var_t::ref lhs,
		ast::expression_t::ref rhs_node,
		bound_var_t::ref rhs,
		ast::item_t::ref obj,
		std::string function_name,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false,
		types::type_t::ref expected_type)
{
	indent_logger indent(obj->get_location(), 6, string_format("checking binary operator " c_id("%s") " with operands %s and %s",
				function_name.c_str(),
				lhs->str().c_str(),
				rhs->str().c_str()));
	auto lhs_type = lhs->type->get_type()->eval(scope);
	auto rhs_type = rhs->type->get_type()->eval(scope);

	debug_above(5, log("generating binary operator %s %s %s", 
				lhs->type->str().c_str(),
				function_name.c_str(),
				rhs->type->str().c_str()));

	bool lhs_is_null = lhs_type->eval_predicate(tb_null, scope);
	bool rhs_is_null = rhs_type->eval_predicate(tb_null, scope);

	if ((lhs_type->repr() == MBS_TYPE || rhs_type->repr() == MBS_TYPE) &&
			(!lhs_is_null && !rhs_is_null))
	{
		/* intercept *char operations */
		if (function_name == "__binary_eq__" && function_name == "__binary_ineq__") {
			return resolve_native_pointer_binary_operation(builder, scope, life,
					obj->get_location(), lhs_node, lhs, rhs_node, rhs, function_name, scope_if_true, scope_if_false,
					expected_type);
		} else {
			return call_module_function(
					builder, scope, life, function_name,
					obj->get_location(), {lhs, rhs});
		}
	}

	if (
			lhs->type->get_llvm_type()->isIntegerTy() &&
			rhs->type->get_llvm_type()->isIntegerTy() &&
			!lhs_type->eval_predicate(tb_bool, scope) &&
			!rhs_type->eval_predicate(tb_bool, scope))
	{
		/* we are dealing with two integers, standard function resolution rules do not apply */
		return type_check_binary_integer_op(
				builder, scope, life,
				obj->get_location(),
				lhs,
				rhs,
				function_name,
				expected_type);
	} else {
		/* intercept binary operations on native pointers */
		if (
				(lhs->type->is_function(scope)
				 || lhs->type->is_ptr(scope)
				 || lhs_is_null) &&
				(rhs->type->is_function(scope)
				 || rhs->type->is_ptr(scope)
				 || rhs_is_null))
		{
			bool lhs_is_managed;
			lhs->type->is_managed_ptr(
					builder,
					scope,
					lhs_is_managed);
			if (!lhs_is_managed || rhs_is_null) {
				bool rhs_is_managed;
				rhs->type->is_managed_ptr(
						builder, scope,
						rhs_is_managed);
				if (!rhs_is_managed || lhs_is_null) {
					/* yeah, it looks like we are operating on two native pointers */
					return resolve_native_pointer_binary_operation(builder, scope, life,
							obj->get_location(), lhs_node, lhs, rhs_node, rhs, function_name, scope_if_true, scope_if_false,
							expected_type);
				}
			}
		}

		/* get or instantiate a function we can call on these arguments */
		auto value = call_module_function(
				builder, scope, life, function_name,
				obj->get_location(), {lhs, rhs});
		return value;
	}
}

bound_var_t::ref type_check_binary_operator(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		ptr<const ast::expression_t> lhs,
		ptr<const ast::expression_t> rhs,
		ast::item_t::ref obj,
		std::string function_name,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false,
		types::type_t::ref expected_type)
{
	assert(function_name.size() != 0);

	bound_var_t::ref lhs_var, rhs_var;
	lhs_var = lhs->resolve_expression(builder, scope, life, false /*as_ref*/, nullptr);
	assert(!lhs_var->type->is_ref(scope));

	rhs_var = rhs->resolve_expression(builder, scope, life, false /*as_ref*/, nullptr);

	assert(!rhs_var->type->is_ref(scope));

	return type_check_binary_operator(
			builder, scope, life, lhs, lhs_var, rhs, rhs_var, obj,
			function_name, scope_if_true, scope_if_false, expected_type);
}


bound_var_t::ref type_check_binary_equality(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		ptr<const ast::expression_t> lhs,
		ptr<const ast::expression_t> rhs,
		ast::item_t::ref obj,
		std::string function_name,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false,
		types::type_t::ref expected_type)
{
	bound_var_t::ref lhs_var, rhs_var;
	lhs_var = lhs->resolve_expression(builder, scope, life,
			false /*as_ref*/, nullptr);
	rhs_var = rhs->resolve_expression(builder, scope, life,
			false /*as_ref*/, nullptr);

	assert(!lhs_var->type->is_ref(scope));
	assert(!rhs_var->type->is_ref(scope));
	bool negated = (function_name == "__ineq__" || function_name == "__isnot__");
	return resolve_native_pointer_binary_compare(builder, scope, life, obj->get_location(), lhs, lhs_var, rhs, rhs_var,
			negated ? rnpbc_ineq : rnpbc_eq, scope_if_true, scope_if_false, expected_type);
}

bound_var_t::ref ast::binary_operator_t::resolve_expression(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);

	if (token.is_ident(K(is))) {
		return type_check_binary_equality(builder, runnable_scope, life, lhs, rhs,
				shared_from_this(), function_name, nullptr, nullptr, expected_type);
	}

	return type_check_binary_operator(builder, runnable_scope, life, lhs, rhs,
			shared_from_this(), function_name, nullptr, nullptr, expected_type);
}

bound_var_t::ref ast::binary_operator_t::resolve_condition(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		types::type_t::ref expected_type,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false) const
{
	if (token.is_ident(K(is))) {
		return type_check_binary_equality(builder, scope, life, lhs, rhs,
				shared_from_this(), function_name, scope_if_true, scope_if_false,
				expected_type);
	}

	return type_check_binary_operator(builder, scope, life, lhs, rhs,
			shared_from_this(), function_name, scope_if_true, scope_if_false,
			expected_type);
}



bound_var_t::ref ast::tuple_expr_t::resolve_expression(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	if (expected_type != nullptr) {
		debug_above(7, log("tuple literal is expected to return a %s", expected_type->str().c_str()));
	}
	types::type_product_t::ref expected_product = dyncast<const types::type_product_t>(expected_type);

    if (values.size() == 0) {
        /* the unit */
        auto unit = scope->get_program_scope()->get_singleton("__unit__");
        assert(unit != nullptr);
        return unit;
    }

	/* let's get the actual values in our tuple. */
	bound_var_t::refs vars;
	vars.reserve(values.size());
	types::type_t::refs expected_dimensions;

	if (expected_product != nullptr) {
		expected_dimensions = expected_product->get_dimensions();
		if (expected_dimensions.size() != values.size()) {
			throw user_error(get_location(), "need %d items in tuple literal to match the expected type %s",
					(int)values.size(), expected_product->str().c_str());
		}
	}

	int i = 0;
	for (auto &value: values) {
		bound_var_t::ref var = value->resolve_expression(builder,
				scope, life, false /*as_ref*/,
				expected_product != nullptr ? expected_dimensions[i] : nullptr);
		vars.push_back(var);
		++i;
	}

	bound_type_t::refs args = get_bound_types(vars);

	/* let's get the type for this tuple wrapped as an object */
	types::type_tuple_t::ref tuple_type = get_tuple_type(args);

	/* now, let's see if we already have a ctor for this tuple type, if not
	 * we'll need to create a data ctor for this unnamed tuple type */
	auto program_scope = scope->get_program_scope();

	std::pair<bound_var_t::ref, bound_type_t::ref> tuple = upsert_tuple_ctor(
			builder, scope, tuple_type, shared_from_this());

	/* now, let's call our unnamed tuple ctor and return that value */
	return create_callsite(builder, scope, life,
			tuple.first, tuple_type->repr(),
			token.location, vars);
}

enum rct_t {
	rct_and,
	rct_or,
	rct_ternary,
};

const char *rctstr(rct_t rct) {
	switch (rct) {
	case rct_and:
		return "and";
	case rct_or:
		return "or";
	case rct_ternary:
		return "ternary";
	}
	panic("unreachable rctstr");
	return "";
}


bound_type_t::ref refine_conditional_type(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		types::type_t::ref condition_type,
		types::type_t::ref truthy_path_type,
		types::type_t::ref falsey_path_type,
		rct_t rct)
{
	debug_above(7, log("refining " c_ast("%s") " expression type %s with truthy path %s and falsey path %s",
				rctstr(rct),
				condition_type->str().c_str(),
				truthy_path_type->str().c_str(),
				falsey_path_type->str().c_str()));
	switch (rct) {
	case rct_or:
		/* we can remove falsey types from the truthy path type */
		truthy_path_type = truthy_path_type->boolean_refinement(false, scope);
		break;
	case rct_and:
		/* we can remove truthy types from the truthy path type */
		falsey_path_type = falsey_path_type->boolean_refinement(true, scope);
		break;
	case rct_ternary:
		/* we can't remove anything */
		break;
	}

	if (condition_type->boolean_refinement(false, scope) == nullptr) {
		/* the condition value was definitely falsey */
		/* factor out the truthy path type entirely */
		truthy_path_type = nullptr;
	} else if (condition_type->boolean_refinement(true, scope) == nullptr) {
		/* the condition value was definitely truthy */
		/* factor out the falsey path type entirely */
		falsey_path_type = nullptr;
	}

	assert((truthy_path_type != nullptr) || (falsey_path_type != nullptr));

	types::type_t::ref ternary_sum_type;
	if (truthy_path_type == nullptr) {
		ternary_sum_type = falsey_path_type;
	} else if (falsey_path_type == nullptr) {
		ternary_sum_type = truthy_path_type;
	} else if (unifies(truthy_path_type, falsey_path_type, scope)) {
		ternary_sum_type = truthy_path_type;
	} else if (unifies(falsey_path_type, truthy_path_type, scope)) {
		ternary_sum_type = falsey_path_type;
	} else if (truthy_path_type->eval_predicate(tb_null, scope)) {
		assert(types::is_managed_ptr(falsey_path_type, scope));
		ternary_sum_type = type_maybe(falsey_path_type, scope);
	} else if (falsey_path_type->eval_predicate(tb_null, scope)) {
		assert(types::is_managed_ptr(truthy_path_type, scope));
		ternary_sum_type = type_maybe(truthy_path_type, scope);
	} else {
		auto error = user_error(location, "ternary type is inconsistent");
		error.add_info(truthy_path_type->get_location(), "truthy path is type %s", truthy_path_type->str().c_str());
		error.add_info(truthy_path_type->get_location(), "falsey path is type %s", falsey_path_type->str().c_str());
		throw error;
	}

	return upsert_bound_type(builder, scope, ternary_sum_type);
}

bound_var_t::ref resolve_cond_expression( /* ternary expression */
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		ast::condition_t::ref condition,
		ast::expression_t::ref when_true,
		ast::expression_t::ref when_false,
		identifier::ref value_name,
		types::type_t::ref expected_type,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false)
{
	/* these scopes are calculated for the interior conditional branching in order to provide refined types for the
	 * when_true or when_false branches */
	runnable_scope_t::ref inner_scope_if_true;
	runnable_scope_t::ref inner_scope_if_false;

	indent_logger indent(condition->get_location(), 6, string_format("resolving ternary expression (%s) ? (%s) : (%s)",
				condition->str().c_str(), when_true->str().c_str(), when_false->str().c_str()));

	/* if scope allows us to set up new variables inside if conditions */
	bound_var_t::ref condition_value = condition->resolve_condition(
			builder, scope, life,
		   	type_id(make_iid(BOOL_TYPE)),
		   	&inner_scope_if_true, &inner_scope_if_false);

	if ((condition != when_false) && (condition != when_true)) {
		/* this is a regular ternary expression. for now, there are 4 paths through in terms of truthiness and
		 * falsiness, and there is no way to propagate out the learned truths from the inner conditional branch because
		 * the when_true and when_false branches may yield differing notions of t/f which cannot be distilled into 2
		 * matching truths. */
	}

	/* evaluate the condition for branching */
	debug_above(7, log("conditional expression has condition of type %s", condition_value->type->str().c_str()));

	assert(!condition_value->type->is_ref(scope));

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

	/* create the inner branch instruction */
	llvm_create_if_branch(builder, scope, 0,
			life, condition->get_location(), condition_value,
			(condition == when_true) /* allow_maybe_check only in "or" expressions */,
			then_bb, else_bb);

	/* calculate the false path's value in the else block */
	builder.SetInsertPoint(else_bb);
	bound_var_t::ref false_path_value;
	if (condition == when_false) {
		/* this is an AND expression, so don't recompute the false value */
		false_path_value = condition_value;
	} else if (condition == when_true) {
		/* this is an OR expression, so compute the second term, and build upon any type
		 * refinements we've acquired so far. */
		if (scope_if_false != nullptr && inner_scope_if_false != nullptr) {
			*scope_if_false = inner_scope_if_false;
		}
		false_path_value = when_false->resolve_condition(
				builder, inner_scope_if_false ? inner_scope_if_false : scope, life,
				expected_type, nullptr, scope_if_false);
	} else {
		/* this is a TERNARY expression, so compute the third term, and do not return any
		 * type refinements, because there is no way to discern where the truthy or
		 * falseyness of this entire expression came from (in the context of our parent
		 * conditional form. */
		false_path_value = when_false->resolve_condition(
				builder, inner_scope_if_false ? inner_scope_if_false : scope, life,
				expected_type, nullptr, nullptr);
	}

	/* after calculation, the code should jump to the phi node's basic block */
	assert(!builder.GetInsertBlock()->getTerminator());
	llvm::Instruction *false_merge_branch = builder.CreateBr(merge_bb);

	/* let's generate code for the "true-path" block */
	builder.SetInsertPoint(then_bb);

	/* get the bound_var for the truthy path */
	bound_var_t::ref true_path_value;
	if (condition == when_true) {
		/* this is an OR expression, so don't recompute the true value */
		true_path_value = condition_value;
	} else if (condition == when_false) {
		/* this is an AND expression, so compute the second term, and build upon any
		 * type refinements we've acquired so far. */
		if (scope_if_true != nullptr && inner_scope_if_true != nullptr) {
			*scope_if_true = inner_scope_if_true;
		}
		true_path_value = when_true->resolve_condition(
				builder, inner_scope_if_true ? inner_scope_if_true : scope, life,
				expected_type, scope_if_true, nullptr);
	} else {
		/* this is a TERNARY expression, so compute the third term, and do not return
		 * any type refinements, because there is no way to discern where the truthy or
		 * falseyness of this entire expression came from (in the context of our parent
		 * conditional form. */
		true_path_value = when_true->resolve_condition(
				builder, inner_scope_if_true ? inner_scope_if_true : scope, life,
				expected_type, nullptr, nullptr);
	}

	bound_type_t::ref ternary_type = refine_conditional_type(
			builder, scope,
			condition->get_location(),
			condition_value->type->get_type(),
			true_path_value->type->get_type(),
			false_path_value->type->get_type(),
			(condition == when_true) ? rct_or
			: (condition == when_false ? rct_and
				: rct_ternary));

	assert(!builder.GetInsertBlock()->getTerminator());
	llvm::Instruction *truthy_merge_branch = builder.CreateBr(merge_bb);
	builder.SetInsertPoint(merge_bb);

	llvm::PHINode *llvm_phi_node = llvm::PHINode::Create(
			ternary_type->get_llvm_specific_type(),
			2, "ternary.phi.node", merge_bb);

	llvm::Value *llvm_truthy_path_value = nullptr;
	/* BLOCK */ {
		/* make sure that we cast the incoming phi value to the
		 * final type in the incoming BB, not in the merge BB */
		llvm::IRBuilder<> builder(truthy_merge_branch);

		llvm_truthy_path_value = coerce_value(
				builder, scope, life,
				condition->get_location(),
				ternary_type->get_type(),
				true_path_value);
		llvm_phi_node->addIncoming(llvm_truthy_path_value, builder.GetInsertBlock());
	}

	llvm::Value *llvm_false_path_value = nullptr;
	/* BLOCK */ {
		/* make sure that we cast the incoming phi value to the
		 * final type in the incoming BB, not in the merge BB */
		llvm::IRBuilder<> builder(false_merge_branch);
		llvm_false_path_value = coerce_value(
				builder, scope, life,
				condition->get_location(),
				ternary_type->get_type(),
				false_path_value);
		llvm_phi_node->addIncoming(llvm_false_path_value, builder.GetInsertBlock());
	}

	debug_above(6, log("ternary expression resolved to type %s",
				ternary_type->str().c_str()));
	return bound_var_t::create(
			INTERNAL_LOC(),
			{"ternary.value"},
			ternary_type,
			llvm_phi_node,
			value_name);
}

bound_var_t::ref ast::ternary_expr_t::resolve_expression(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
	return resolve_cond_expression(builder, runnable_scope, life, as_ref,
			condition, when_true, when_false,
			make_code_id(this->token), expected_type, nullptr, nullptr);
}

bound_var_t::ref ast::ternary_expr_t::resolve_condition(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		types::type_t::ref expected_type,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false) const
{
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
	return resolve_cond_expression(builder, runnable_scope, life, false /*as_ref*/,
			condition, when_true, when_false,
			make_code_id(this->token), expected_type, scope_if_true, scope_if_false);
}

bound_var_t::ref ast::or_expr_t::resolve_expression(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
	return resolve_cond_expression(builder, runnable_scope, life, as_ref,
			lhs, lhs, rhs, make_iid("or.value"), expected_type, nullptr, nullptr);
}

bound_var_t::ref ast::or_expr_t::resolve_condition(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		types::type_t::ref expected_type,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false) const
{
	return resolve_cond_expression(builder, scope, life, false /*as_ref*/,
			lhs, lhs, rhs, make_iid("or.value"), expected_type, scope_if_true, scope_if_false);
}

bound_var_t::ref ast::and_expr_t::resolve_expression(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
	return resolve_cond_expression(builder, runnable_scope, life, as_ref,
			lhs, rhs, lhs, make_iid("and.value"), expected_type, nullptr, nullptr);
}

bound_var_t::ref ast::and_expr_t::resolve_condition(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		types::type_t::ref expected_type,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false) const
{
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
	return resolve_cond_expression(builder, runnable_scope, life, false /*as_ref*/,
			lhs, rhs, lhs, make_iid("and.value"), expected_type, scope_if_true, scope_if_false);
}

types::type_t::ref extract_matching_type(
		identifier::ref type_var_name,
	   	types::type_t::ref actual_type,
		types::type_t::ref pattern_type)
{
	unification_t unification = unify(pattern_type, actual_type, {});
	if (unification.result) {
		return unification.bindings[type_var_name->get_name()];
	} else {
		return nullptr;
	}
}

bound_var_t::ref extract_member_variable(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		bound_var_t::ref bound_var,
		std::string member_name,
		bool as_ref,
		types::type_t::ref expected_type)
{
	bound_var = bound_var->resolve_bound_value(builder, scope);

	auto expanded_type = bound_var->type->get_type()->eval(scope, true);
	bound_type_t::ref bound_obj_type = upsert_bound_type(builder, scope, expanded_type);

	types::type_struct_t::ref struct_type = get_struct_type_from_bound_type(
			scope, location, bound_obj_type);
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

		return extract_member_by_index(builder, scope, 
				life, location, bound_var, bound_obj_type, index,
				member_name, as_ref);

	} else {
		auto bindings = scope->get_type_variable_bindings();
		auto full_type = bound_var->type->get_type()->rebind(bindings);
		auto error = user_error(location,
				"%s has no dimension called " c_id("%s"),
				full_type->str().c_str(),
				member_name.c_str());
		error.add_info(bound_var->type->get_location(), "%s has dimension(s) [%s]",
				full_type->str().c_str(),
				join_with(member_index, ", ", [] (std::pair<std::string, int> index) -> std::string {
					return std::string(C_ID) + index.first + C_RESET;
					}).c_str());
		throw error;
	}
}

bound_var_t::ref resolve_module_variable_reference(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		std::string module_name,
		std::string symbol,
		bool as_ref)
{
	std::string qualified_id = string_format("%s%s%s",
			module_name.c_str(),
			SCOPE_SEP,
			symbol.c_str());

	debug_above(5,
			log("attempt to find global id " c_id("%s"),
				qualified_id.c_str()));
	bound_var_t::ref var = scope->get_bound_variable(builder, location, qualified_id);

	/* if we couldn't resolve that id, let's look for unchecked variables */
	program_scope_t::ref program_scope = scope->get_program_scope();
	if (var == nullptr) {
		if (unchecked_var_t::ref unchecked_var = program_scope->get_unchecked_variable(qualified_id)) {
			if (ast::var_decl_t::ref var_decl = dyncast<const ast::var_decl_t>(unchecked_var->node)) {
				var = upsert_module_variable(
						builder,
						unchecked_var->module_scope,
						*var_decl,
						symbol);
			} else {
				throw user_error(location, "symbol " c_module("%s") "." c_id("%s") " is either ambiguous in this context or does not exist",
						module_name.c_str(),
						symbol.c_str());
			}
		}
	}

	/* now, let's make sure to avoid returning refs if !as_ref */
	if (var != nullptr) {
		if (!as_ref) {
			/* if we're not asking for a ref, then get rid of it if it's there */
			return var->resolve_bound_value(builder, scope);
		} else {
			return var;
		}
	} else {
		/* check for unbound module variable */
		throw user_error(location, "could not find symbol " c_id("%s"), qualified_id.c_str());
	}
}

bound_var_t::ref ast::dot_expr_t::resolve_expression(
        llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	debug_above(6, log("resolving dot_expr %s", str().c_str()));
	bound_var_t::ref lhs_val = lhs->resolve_expression(
			builder, scope, life, false /*as_ref*/, nullptr);

	types::type_t::ref member_type;

	if (lhs_val->type->is_module()) {
		return resolve_module_variable_reference(builder, scope, get_location(),
				lhs_val->name, rhs.text, as_ref);
	} else {
		return extract_member_variable(builder, scope, life, get_location(),
				lhs_val, rhs.text, as_ref, expected_type);
	}
}

bound_var_t::ref cast_bound_var(
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		life_t::ref life,
		location_t location,
		bound_var_t::ref bound_var,
		types::type_t::ref type_cast,
		bool force_cast)
{
	assert(!bound_var->type->is_ref(scope));
	if (bound_var->type->is_maybe(scope) && !type_cast->eval_predicate(tb_maybe, scope)) {
		auto error = user_error(location, "you cannot safely cast away maybe. use the ! operator instead");
		error.add_info(location, "better yet, use an if statement to check the return value so you don't accidentally dereference a null pointer. assertions also work.");
		throw error;
	}

	bound_type_t::ref bound_type = upsert_bound_type(builder, scope, type_cast);
	debug_above(7, log("upserted bound type in cast expr is %s", bound_type->str().c_str()));
	indent_logger indent(location, 5, string_format("casting %s: %s (%s) to a %s (%s)",
				bound_var->name.c_str(),
				bound_var->type->get_type()->str().c_str(),
				llvm_print(bound_var->get_llvm_value()->getType()).c_str(),
				type_cast->str().c_str(),
				llvm_print(bound_type->get_llvm_specific_type()).c_str()));
	llvm::Value *llvm_source_val = bound_var->resolve_bound_var_value(scope, builder);
	llvm::Type *llvm_source_type = llvm_source_val->getType();

	llvm::Value *llvm_dest_val = nullptr;
	llvm::Type *llvm_dest_type = bound_type->get_llvm_specific_type();

	if (!force_cast) {
		/* don't let anyone cast pointers unless they are forcing it! */
		if (llvm_source_type->isPointerTy() || llvm_dest_type->isPointerTy()) {
			auto error = user_error(location, "you cannot safely cast user-defined types like this. if you must be unsafe, use \"as!\".");
			error.add_info(location, "attempt to cast a value of type %s to a %s",
					bound_var->type->get_type()->str().c_str(),
					type_cast->str().c_str());
			throw error;
		}
	}

	// TODO: put some more constraints on this...
	if (llvm_dest_type->isIntegerTy()) {
		/* we want an integer at the end... */
		if (llvm_source_type->isPointerTy()) {
			llvm_dest_val = builder.CreatePtrToInt(llvm_source_val, llvm_dest_type);
		} else {
			assert(llvm_source_type->isIntegerTy());
			llvm_dest_val = builder.CreateSExtOrTrunc(llvm_source_val, llvm_dest_type);
		}
	} else if (llvm_dest_type->isPointerTy()) {
		/* we want a pointer at the end... */
		if (llvm_source_type->isPointerTy()) {
			llvm_dest_val = builder.CreateBitCast(llvm_source_val, llvm_dest_type);
		} else {
			if (!llvm_source_type->isIntegerTy()) {
				throw user_error(location, "unsure how to cast from %s to %s",
						llvm_print(llvm_source_type).c_str(),
						type_cast->str().c_str());
			}
			llvm_dest_val = builder.CreateIntToPtr(llvm_source_val, llvm_dest_type);
		}
	} else {
		throw user_error(location, "invalid cast: cannot cast %s to %s",
				bound_var->type->str().c_str(),
				type_cast->str().c_str());
	}

	return bound_var_t::create(INTERNAL_LOC(), "cast", bound_type, llvm_dest_val, make_iid_impl("cast",
				bound_var->get_location()));
}

bound_var_t::ref call_get_ctor_id(
		scope_t::ref scope,
		life_t::ref life,
		ast::item_t::ref callsite,
		identifier::ref id,
		llvm::IRBuilder<> &builder,
		bound_var_t::ref resolved_value)
{
	resolved_value = resolved_value->resolve_bound_value(builder, scope);
	indent_logger indent(callsite->get_location(), 4, string_format("getting typeid of %s",
				resolved_value->type->str().c_str()));
	auto program_scope = scope->get_program_scope();

	bool is_managed = false;
	resolved_value->type->is_managed_ptr(
			builder,
			scope,
			is_managed);
	if (is_managed) {
		bound_var_t::ref bound_managed_var = cast_bound_var(
				builder,
				scope,
				life,
				callsite->get_location(),
				resolved_value,
				type_ptr(type_id(make_iid(STD_MANAGED_TYPE))),
				true /*force_cast*/);
		auto name = string_format("typeid(%s)", resolved_value->str().c_str());

		bound_var_t::ref get_typeid_function = get_callable(
				builder,
				scope,
				"runtime.__get_ctor_id",
				callsite->get_location(),
				type_args({bound_managed_var->type->get_type()}),
				type_variable(INTERNAL_LOC()));

		assert(get_typeid_function != nullptr);
		return create_callsite(
				builder,
				scope,
				life,
				get_typeid_function,
				name,
				id->get_location(),
				{bound_managed_var});
	} else {
		// There is no type info here, so...
		throw user_error(callsite->get_location(), "data of type %s has no runtime type information",
				resolved_value->type->str().c_str());

		return nullptr;
	}
}


bound_var_t::ref ast::typeid_expr_t::resolve_expression(
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	assert(!as_ref);

	auto resolved_value = expr->resolve_expression(
			builder,
			scope,
			life,
			false /*as_ref*/,
			nullptr);

	return call_get_ctor_id(scope, life, shared_from_this(), make_code_id(token), builder, resolved_value);
}

bound_var_t::ref ast::sizeof_expr_t::resolve_expression(
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	assert(!as_ref);

	/* calculate the size of the object being referenced assume native types */
	bound_type_t::ref bound_type = upsert_bound_type(builder, scope, type->rebind(scope->get_type_variable_bindings()));
	bound_type_t::ref size_type = upsert_bound_type(builder, scope->get_program_scope(), type_id(make_iid("size_t")));
	llvm::Value *llvm_size = llvm_sizeof_type(builder,
			llvm_deref_type(bound_type->get_llvm_specific_type()));

	return bound_var_t::create(
			INTERNAL_LOC(), type->str(), size_type, llvm_size,
			make_iid("sizeof"));
}


bound_var_t::ref ast::function_defn_t::resolve_expression(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	assert(!as_ref);
	expected_type = types::freshen(expected_type ? expected_type->rebind(scope->get_type_variable_bindings()) : nullptr);

	debug_above(6, log("resolving function expression with declared signature %s at %s with expected type %s",
				decl->function_type->str().c_str(),
				token.location.str().c_str(),
				expected_type ? expected_type->str().c_str() : "<null>"));
	auto runnable_scope = dyncast<runnable_scope_t>(scope);
	if (runnable_scope != nullptr) {
		/* we are instantiating a function within a runnable scope, let's get closure over the environment we're in */
		auto closure_name = std::string("anonymous fn ") + decl->function_type->repr() + " at " + token.location.repr();
		auto closure_scope = runnable_scope->new_closure_scope(builder, closure_name);
		auto function = resolve_function(builder, closure_scope, life, true /*as_closure*/, expected_type, nullptr, nullptr);

		auto r = closure_scope->create_closure(builder, life, get_location(), function);
		return r;
	} else {
		return resolve_function(builder, scope, life, false /*as_closure*/, expected_type, nullptr, nullptr);
	}
}

void ast::function_defn_t::resolve_statement(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	resolve_function(builder, scope, life, false /*as_closure*/, nullptr/*expected_type*/, new_scope, returns);
}

bound_var_t::ref ast::function_defn_t::resolve_function(
        llvm::IRBuilder<> &builder_,
        scope_t::ref scope,
		life_t::ref,
		bool as_closure,
		types::type_t::ref expected_type,
		runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	llvm::IRBuilder<> builder(builder_.getContext());

	/* lifetimes have extents at function boundaries */
	auto life = make_ptr<life_t>(lf_function);

	/* function definitions are type checked at instantiation points. callsites
	 * are instantiation points.
	 *
	 * The main job of this function is to:
	 * 0. type check the function given the scope.
	 * 1. generate code for this function.
	 * 2. bind the function name to the generated code within the given scope.
	 * */
	INDENT(2, string_format(
				"type checking %s in %s with type variable bindings %s",
				token.str().c_str(),
				scope->get_name().c_str(),
				::str(scope->get_type_variable_bindings()).c_str()));

	assert_implies(as_closure, dyncast<closure_scope_t>(scope) != nullptr);

	/* see if we can get a monotype from the function declaration */
	types::type_function_t::ref fn_type;
	types::type_t::ref type_constraints;
	bound_type_t::named_pairs args;
	bound_type_t::ref return_type;
	bool needs_type_fixup = false;

	destructure_function_decl(builder, *decl, scope, type_constraints,
			as_closure, needs_type_fixup, args, return_type, fn_type, expected_type);

	return instantiate_function_with_args_and_return_type(builder, scope, life, token, as_closure,
			needs_type_fixup, decl->extends_module, new_scope, type_constraints, args, return_type,
			fn_type, block);
}

void type_check_module_links(
		compiler_t &compiler,
		llvm::IRBuilder<> &builder,
		const ast::module_t &obj,
		scope_t::ref program_scope)
{
	INDENT(3, string_format("resolving links in " c_module("%s"),
				obj.module_key.c_str()));

	/* get module level scope variable */
	module_scope_t::ref scope = compiler.get_module_scope(obj.module_key);

	for (const ptr<ast::link_module_statement_t> &link : obj.linked_modules) {
		link->resolve_statement(builder, scope, nullptr, nullptr,
				nullptr);
	}

	for (const ptr<ast::link_function_statement_t> &link : obj.linked_functions) {
		bound_var_t::ref link_value = link->resolve_expression(
				builder, scope, nullptr, false /*as_ref*/, nullptr);

		if (link->extern_function->token.text.size() != 0) {
			put_bound_function(
					scope,
					link->extern_function->get_location(),
					link->extern_function->token.text,
					link->extern_function->extends_module,
					link_value,
					nullptr);
		} else {
			throw user_error(link->get_location(), "module level link definitions need names");
		}
	}

	for (const ptr<ast::link_var_statement_t> &link : obj.linked_vars) {
		bound_var_t::ref link_value = link->resolve_expression(
				builder, scope, nullptr, false /*as_ref*/, nullptr);

		scope->put_bound_variable(link->var_decl->get_symbol(), link_value);
	}
}

void type_check_module_vars(
        compiler_t &compiler,
		llvm::IRBuilder<> &builder,
		const ast::module_t &obj,
		scope_t::ref program_scope,
		std::vector<bound_var_t::ref> &global_vars)
{
	indent_logger indent(obj.get_location(), 2, string_format("resolving module variables in " c_module("%s"),
				obj.module_key.c_str()));

	/* get module level scope variable */
	module_scope_t::ref module_scope = compiler.get_module_scope(obj.module_key);
	for (auto &var_decl : obj.var_decls) {
		try {
			INDENT(3, string_format("resolving module var " c_id("%s") " in " c_module("%s"),
						module_scope->make_fqn(var_decl->token.text).c_str(),
						obj.module_key.c_str()));

			/* the idea here is to put this variable into module scope,
			 * available globally, but to initialize it in the
			 * __init_module_vars function */
			auto module_var = type_check_module_var_decl(builder, module_scope, *var_decl);
			global_vars.push_back(module_var);
		} catch (user_error &e) {
			std::throw_with_nested(user_error(log_info, var_decl->get_location(),
						"while checking module variable %s",
						var_decl->token.text.c_str()));
		} catch (std::exception &e) {
			fprintf(stderr, c_error("FAIL: ") "%s\n", e.what());
			throw;
		} catch (...) {
			panic("uncaught exception");
		}
	}
}

void resolve_unchecked_type(
		llvm::IRBuilder<> &builder,
		module_scope_t::ref module_scope,
		unchecked_type_t::ref unchecked_type)
{
	auto program_scope = module_scope->get_program_scope();
	auto node = unchecked_type->node;

	assert(!dyncast<const ast::function_defn_t>(node));

	debug_above(5, log(log_info, "checking module level type %s", node->token.str().c_str()));

	/* these next lines create type definitions, regardless of
	 * their genericity.  type expressions will be added as
	 * environment variables in the type system.  this step is
	 * MUTATING the type environment of the module, and the
	 * program. */
	if (auto type_def = dyncast<const ast::type_def_t>(node)) {
		type_def->resolve_statement(builder,
				module_scope, nullptr, nullptr, nullptr);
	} else {
		panic("unhandled unchecked type node at module scope");
	}
}

void type_check_module_types(
		compiler_t &compiler,
		llvm::IRBuilder<> &builder,
		const ast::module_t &obj,
		scope_t::ref program_scope)
{
	INDENT(2, string_format("type-checking types in module " c_module("%s"),
				obj.module_key.c_str()));

	/* get module level scope types */
	module_scope_t::ref module_scope = compiler.get_module_scope(obj.module_key);

	auto unchecked_types_ordered = module_scope->get_unchecked_types_ordered();
	for (unchecked_type_t::ref unchecked_type : unchecked_types_ordered) {
		resolve_unchecked_type(builder, module_scope, unchecked_type);
	}
}

void type_check_program_variable(
		llvm::IRBuilder<> &builder,
		program_scope_t::ref program_scope,
		unchecked_var_t::ref unchecked_var)
{
	debug_above(8, log(log_info, "checking whether to check %s", unchecked_var->str().c_str()));

	auto node = unchecked_var->node;

	/* prevent recurring checks */
	debug_above(7, log(log_info, "checking module level variable %s", node->token.str().c_str()));
	if (auto function_defn = dyncast<const ast::function_defn_t>(node)) {
		if (is_function_decl_generic(unchecked_var->module_scope, *function_defn)) {
			/* this is a generic function so we need not check it now */
			return;
		}

		if (getenv("MAIN_ONLY") != nullptr && node->token.text != "__main__") {
			debug_above(8, log(log_info, "skipping %s because it's not '__main__'",
						node->str().c_str()));
			return;
		}
		types::type_t::ref type_constraints;
		types::type_function_t::ref function_type;
		bound_type_t::named_pairs named_params;
		bound_type_t::ref return_value;
        bool needs_type_fixup = false;

		destructure_function_decl(
				builder,
				*function_defn->decl,
				unchecked_var->module_scope,
				type_constraints,
				false /*as_closure*/,
                needs_type_fixup,
				named_params,
				return_value,
				function_type,
				nullptr);

        assert(!needs_type_fixup);

		var_t::refs fns;
		fittings_t fittings;
		bound_var_t::ref callable = maybe_get_callable(
				builder,
				unchecked_var->module_scope,
				function_defn->decl->token.text,
				node->get_location(),
				function_type->args,
				function_type->return_type,
				fns,
				fittings,
				false /*check_unchecked*/,
				false /*allow_coercions*/);

		if (callable != nullptr) {
			/* we've already checked this function */
			// HACKHACK: this is really fragile and probably wrong. Should be checking type-env + signature in some
			// normal form?
			if (callable->get_location() != unchecked_var->id->get_location()) {
				auto error = user_error(
						function_defn->get_location(), "duplicate function %s found",
						function_defn->decl->str().c_str());
				error.add_info(callable->get_location(), "see prior definition here (%s)",
						callable->str().c_str());
				throw error;
			}
			return;
		}
	}

	if (dyncast<const ast::var_decl_t>(node)) {
		/* ignore here */
	} else if (auto stmt = dyncast<const ast::statement_t>(node)) {
		stmt->resolve_statement(
				builder, unchecked_var->module_scope,
				nullptr, nullptr, nullptr);
	} else if (auto product = dyncast<const ast::type_product_t>(node)) {
		/* ignore until instantiation at a callsite */
	} else if (auto data_ctor = dyncast<const ast::data_type_t>(node)) {
		/* ignore until instantiation at a callsite */
	} else {
		log("unchecked node is %s", node->str().c_str());
		dbg();
		panic("unhandled unchecked node at module scope");
	}
}

void type_check_program_variables(
        llvm::IRBuilder<> &builder,
        program_scope_t::ref program_scope)
{
	INDENT(2, string_format("resolving variables in program"));

	auto unchecked_vars_ordered = program_scope->get_unchecked_vars_ordered();
	bool failures = false;
	location_t failure_location;
	for (auto unchecked_var : unchecked_vars_ordered) {
		try {
			type_check_program_variable(builder, program_scope, unchecked_var);
		} catch (user_error &e) {
			/* try to let the compiler recover and keep type checking... */
			if (!failures) {
				failure_location = e.location;
				failures = true;
			}
			print_exception(e);
		}
	}
	if (failures) {
		throw user_error(failure_location, "failures encountered");
	}
}

void create_visit_module_vars_function(
	   	llvm::IRBuilder<> &builder,
	   	program_scope_t::ref program_scope,
		std::vector<bound_var_t::ref> global_vars)
{
	/* build the global __init_module_vars function */
	llvm::IRBuilderBase::InsertPointGuard ipg(builder);

	bound_type_t::ref bound_callback_fn_type = upsert_bound_type(
			builder, program_scope, 
			type_function(
				INTERNAL_LOC(),
				nullptr,
				type_args({type_maybe(type_ptr(type_id(make_iid(STD_MANAGED_TYPE))), {})}),
				type_id(make_iid("void"))));

	/* we are creating this function, but we'll be adding to it elsewhere */
	auto visit_module_vars_fn = llvm_start_function(
			builder, 
			program_scope,
			INTERNAL_LOC(),
			type_function(
				INTERNAL_LOC(),
				nullptr,
				type_args({bound_callback_fn_type->get_type()}),
				program_scope->get_bound_type(VOID_TYPE)->get_type()),
			"__visit_module_vars");

	llvm::Function *llvm_function = llvm::dyn_cast<llvm::Function>(visit_module_vars_fn->get_llvm_value());
	assert(llvm_function != nullptr);
	assert(llvm_function->arg_size() == 1);

	llvm::Value *llvm_visitor_fn = &(*llvm_function->arg_begin());
	auto user_visitor_fn = bound_var_t::create(
			INTERNAL_LOC(),
			"user_visitor_fn",
			bound_callback_fn_type,
			llvm_visitor_fn,
			make_iid("user_visitor_fn"));

	auto bound_var_ptr_type = program_scope->get_runtime_type(builder, STD_MANAGED_TYPE, true /*get_ptr*/);

	for (auto global_var : global_vars) {
		/* for each managed global_var, call the visitor function on it */
		bool is_managed;
		global_var->type->is_managed_ptr(builder, program_scope, is_managed);

		if (is_managed) {
			llvm_create_call_inst(
					builder,
					INTERNAL_LOC(),
					user_visitor_fn,
					std::vector<llvm::Value *>{
					llvm_maybe_pointer_cast(
							builder,
							global_var->resolve_bound_var_value(program_scope, builder),
							bound_var_ptr_type->get_llvm_type())});
		}
	}

	/* we're done with __visit_module_vars, let's make sure to return */
	builder.CreateRetVoid();

	program_scope->put_bound_variable("__visit_module_vars", visit_module_vars_fn);
}

void type_check_all_module_var_slots(
		compiler_t &compiler,
		llvm::IRBuilder<> &builder,
		const ast::program_t &obj,
		program_scope_t::ref program_scope)
{
	std::vector<bound_var_t::ref> global_vars;

	for (auto &module : obj.modules) {
		if (module->module_key == "runtime") {
			assert(!module->global);
			type_check_module_vars(compiler, builder, *module, program_scope,
					global_vars);
			break;
		}
	}

	/* initialized the module-level variable declarations. make sure that we initialize the
	 * runtime variables last. this will add them to the top of the __init_module_vars function. */
	for (auto &module : obj.modules) {
		// if (module->global && module->module_key != "std") {
		// continue;
		// }

		if (module->module_key != "runtime") {
			type_check_module_vars(compiler, builder, *module, program_scope,
					global_vars);
		}
	}

	create_visit_module_vars_function(builder, program_scope, global_vars);
}

void type_check_program(
		llvm::IRBuilder<> &builder,
		const ast::program_t &obj,
		compiler_t &compiler)
{
    INDENT(2, string_format(
                "type-checking program %s",
                compiler.get_program_name().c_str()));

    ptr<program_scope_t> program_scope = compiler.get_program_scope();
    debug_above(11, log(log_info, "type_check_program program scope:\n%s", program_scope->str().c_str()));

    /* pass to resolve all module-level types */
    for (const ast::module_t::ref &module : obj.modules) {
        if (module->global && module->module_key != "std") {
            continue;
        }

        type_check_module_types(compiler, builder, *module, program_scope);
    }

    /* pass to resolve all module-level links */
    for (auto &module : obj.modules) {
        type_check_module_links(compiler, builder, *module, program_scope);
    }

    llvm_create_unit_value(builder, program_scope);

    /* pass to resolve all module-level vars */
    type_check_all_module_var_slots(compiler, builder, obj, program_scope);

    assert(compiler.main_module != nullptr);

    /* pass to resolve all main module-level variables.  technically we only
     * need to check the primary module, since that is the one that is expected
     * to have the entry point ... at least for now... */
    type_check_program_variables(builder, program_scope);
}

void ast::type_def_t::resolve_statement(
		llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        runnable_scope_t::ref *new_scope,
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

	if (auto runnable_scope = dyncast<runnable_scope_t>(scope)) {
		assert(new_scope != nullptr);

		/* type definitions begin new scopes */
		runnable_scope_t::ref fresh_scope = runnable_scope->new_runnable_scope(
				string_format("type-%s", token.text.c_str()));

		/* update current scope for writing */
		scope = fresh_scope;

		/* have the caller update their current scope */
		*new_scope = fresh_scope;
	} else {
		assert(new_scope == nullptr);
	}

	type_algebra->register_type(builder,
			make_code_id(token), type_decl->type_variables, scope);

	return;
}

void ast::assignment_t::resolve_statement(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	assert(token.text == "=");

	if (auto array_index = dyncast<const ast::array_index_expr_t>(lhs)) {
		/* handle assignments into arrays */
		array_index->resolve_assignment(builder, scope, life, false /*as_ref*/, rhs, nullptr);
		return;
	} else {
		auto lhs_var = lhs->resolve_expression(builder, scope, life, true /*as_ref*/, nullptr);
		auto rhs_var = rhs->resolve_expression(builder, scope, life, false /*as_ref*/, types::without_ref(lhs_var->type->get_type()));
		type_check_assignment(builder, scope, life, lhs_var, rhs_var, token.location);
		return;
	}
}

void ast::break_flow_t::resolve_statement(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	if (auto runnable_scope = dyncast<runnable_scope_t>(scope)) {
		llvm::BasicBlock *break_bb = runnable_scope->get_innermost_loop_break();
		if (break_bb != nullptr) {
			/* release everything held back to the loop we're in */
			life->release_vars(builder, scope, lf_loop);

			assert(!builder.GetInsertBlock()->getTerminator());
			builder.CreateBr(break_bb);
			return;
		} else {
			throw user_error(get_location(), c_control("break") " outside of a loop");
		}
	} else {
		panic("we should not be looking at a break statement here!");
		return;
	}
}

void ast::continue_flow_t::resolve_statement(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	if (auto runnable_scope = dyncast<runnable_scope_t>(scope)) {
		llvm::BasicBlock *continue_bb = runnable_scope->get_innermost_loop_continue();
		if (continue_bb != nullptr) {
			/* release everything held back to the loop we're in */
			life->release_vars(builder, scope, lf_loop);

			assert(!builder.GetInsertBlock()->getTerminator());
			builder.CreateBr(continue_bb);
			return;
		} else {
			throw user_error(get_location(), c_control("continue") " outside of a loop");
		}
	} else {
		panic("we should not be looking at a continue statement here!");
		return;
	}
}

bound_var_t::ref type_check_binary_op_assignment(
	   	llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		ast::item_t::ref op_node,
		ast::expression_t::ref lhs,
		ast::expression_t::ref rhs,
		location_t location,
		std::string function_name)
{
	auto lhs_var = lhs->resolve_expression(builder, scope, life, true /*as_ref*/, nullptr);
	bound_var_t::ref lhs_val = lhs_var->resolve_bound_value(builder, scope);

	bound_var_t::ref rhs_var = rhs->resolve_expression(builder, scope, life, false /*as_ref*/, nullptr);

	assert(!rhs_var->type->is_ref(scope));
	auto computed_var = type_check_binary_operator(builder, scope, life, lhs,
			lhs_val, rhs, rhs_var, op_node, function_name, nullptr, nullptr,
			lhs_val->type->get_type());

	return type_check_assignment(builder, scope, life, lhs_var,
			computed_var, location);
}

void ast::mod_assignment_t::resolve_statement(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
	type_check_binary_op_assignment(builder, runnable_scope, life,
			shared_from_this(), lhs, rhs, token.location, "__mod__");
}

void ast::plus_assignment_t::resolve_statement(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
	type_check_binary_op_assignment(builder, runnable_scope, life,
			shared_from_this(), lhs, rhs, token.location, "__plus__");
}

void ast::minus_assignment_t::resolve_statement(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
	type_check_binary_op_assignment(builder, runnable_scope, life,
			shared_from_this(), lhs, rhs, token.location, "__minus__");
}

void ast::unreachable_t::resolve_statement(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        runnable_scope_t::ref *,
		bool *returns) const
{
	*returns = true;
	builder.CreateUnreachable();
	return;
}

void ast::return_statement_t::resolve_statement(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	life = life->new_life(lf_statement);

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
		return_value = expr->resolve_expression(builder, scope, life,
				return_type_constraint ? return_type_constraint->is_ref(scope) : false /*as_ref*/,
				return_type_constraint ? return_type_constraint->get_type() : type_variable(INTERNAL_LOC()));

		/* get the type suggested by this return value */
		return_type = return_value->type;
    } else if (return_type_constraint == nullptr) {
        return_type = upsert_bound_type(builder, scope, type_unit());
    } else {
        return_type = return_type_constraint;
    }

	/* make sure this return type makes sense, or keep track of it if we
	 * didn't yet know the return type for this function */
	runnable_scope->check_or_update_return_type_constraint(shared_from_this(), return_type);

	if (return_value != nullptr) {
		if (return_value->type->is_void(scope)) {
			throw user_error(get_location(),
					"return expressions cannot be " c_type("void") ". use an empty return statement to return from this function");
		} else {
			auto llvm_return_value = coerce_value(
					builder, scope, life,
					return_value->get_location(),
					runnable_scope->get_return_type_constraint()->get_type(),
					return_value);

			if (llvm_return_value->getName().str().size() == 0) {
				llvm_return_value->setName("return.value");
			}

			debug_above(8, log("emitting a return of %s", llvm_print(llvm_return_value).c_str()));

			// BUGBUG: if this were actually releasing variables, this could introduce a period
			// of execution wherein if the garbage collector were to run, the return value could
			// be freed.
			/* release all variables from all lives */
			life->release_vars(builder, scope, lf_function);

			builder.CreateRet(llvm_return_value);
			return;
		}
    } else { 
        /* release all variables from all lives */
        life->release_vars(builder, scope, lf_function);

        /* handle default unspecified type */
        auto bound_unit_value = scope->get_program_scope()->get_singleton("__unit__");
        if (return_type_constraint == nullptr) {
            runnable_scope->check_or_update_return_type_constraint(
                    shared_from_this(),
                    bound_unit_value->type);
            return_type_constraint = runnable_scope->get_return_type_constraint();
        }

        if (return_type_constraint->is_void(scope)) {
            /* we have an empty return in a void function, let's just use void */

            builder.CreateRetVoid();
        } else {
            if (!return_type_constraint->is_unit(scope)) {
                throw user_error(token.location, "invalid empty return. should be of type %s",
                        return_type_constraint->get_type()->str().c_str());
            }

            builder.CreateRet(bound_unit_value->get_llvm_value());
        }
    }
}

void ast::times_assignment_t::resolve_statement(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
	type_check_binary_op_assignment(builder, runnable_scope, life,
			shared_from_this(), lhs, rhs, token.location, "__times__");
}

void ast::divide_assignment_t::resolve_statement(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
	type_check_binary_op_assignment(builder, runnable_scope, life,
			shared_from_this(), lhs, rhs, token.location, "__divide__");
}

void ast::block_t::resolve_statement(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        runnable_scope_t::ref *new_scope,
		bool *returns_) const
{
	resolve_block_expr(
			builder,
		   	scope,
		   	life,
		   	false /*as_ref*/,
		   	returns_,
			type_bottom());
}

bound_var_t::ref ast::block_t::resolve_expression(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	bool returns = false;
	auto block_value = resolve_block_expr(
			builder,
		   	scope,
		   	life,
		   	false /*as_ref*/,
		   	&returns,
		   	expected_type);

	// TODO: consider this...
	// assert(!returns);
	return block_value;
}

bound_var_t::ref ast::block_t::resolve_block_expr(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		bool *returns_,
		types::type_t::ref expected_type) const
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
	life = life->new_life(lf_block);

	bound_var_t::ref block_value;
	for (size_t i = 0; i < statements.size(); ++i) {
		auto &statement = statements[i];

		if (*returns) {
			throw user_error(statement->get_location(), "this statement will never run");
			break;
		}

		runnable_scope_t::ref next_scope;

		debug_above(9, log(log_info, "type checking statement\n%s", statement->str().c_str()));

		try {
			/* create a new life for tracking the rhs values (temp values) in this statement */
			auto stmt_life = life->new_life(lf_statement);

			{
				indent_logger indent(statement->get_location(), 5, string_format("while checking statement %s",
							statement->str().c_str()));

				if (getenv("TRACE_STATEMENTS") != nullptr) {
					std::stringstream ss;
					ss << statement->token.location.str() << ": " << statement->str();
					auto callsite_debug_function_name_print = expand_callsite_string_literal(
							token,
							"posix",
							"puts",
							ss.str());
					callsite_debug_function_name_print->resolve_statement(builder, scope, life, nullptr, nullptr);
				}

				if (expected_type != type_bottom() && i == statements.size() - 1) {
					/* we expect an expression for the final value of this block */
					if (auto expr = dyncast<const expression_t>(statement)) {
						block_value = expr->resolve_expression(
								builder,
								current_scope,
								stmt_life,
								false /*as_ref*/,
								expected_type);

						unification_t unification = unify(expected_type, block_value->type->get_type(), current_scope);
						if (!unification.result) {
							auto error = user_error(block_value->get_location(), "value does not have a cohesive type with the rest of the block");
							error.add_info(expected_type == type_unit() ? token.location : expected_type->get_location(), "expected type %s", expected_type->str().c_str());
							throw error;
						} else {
							/* update expected type to ensure we are narrowing what is acceptable */
							expected_type = expected_type->rebind(unification.bindings);
							assert(expected_type != type_bottom());
						}
					} else if (dyncast<const return_statement_t>(statement) ||
							dyncast<const continue_flow_t>(statement) ||
							dyncast<const break_flow_t>(statement))
				   	{
						/* this block does not yield a value, it just jumps to the outer block */
						expected_type = type_bottom();

						/* resolve the statement */
						statement->resolve_statement(builder, current_scope, stmt_life,
								&next_scope, returns);
					} else {
						throw user_error(statement->get_location(), "expected an expression here");
					}
				} else {
					/* resolve the statement */
					statement->resolve_statement(builder, current_scope, stmt_life,
							&next_scope, returns);
				}
			}

			if (!*returns) {
				/* inject release operations for rhs values out of extent */
				stmt_life->release_vars(builder, scope, lf_statement);
			}

			if (next_scope != nullptr) {
				/* the statement just executed wants to create a new nested scope.
				 * let's allow this by just keeping track of the current scope. */
				current_scope = next_scope;
				next_scope = nullptr;
				debug_above(10, log(log_info, "got a new scope %s", current_scope->str().c_str()));
			}
		} catch (user_error &e) {
			std::throw_with_nested(user_error(log_info, statement->get_location(), "while checking statement"));
		} catch (unbound_type_error &e) {
			std::throw_with_nested(e.user_error); // user_error(log_info, statement->get_location(), "while checking statement"));
		} catch (std::exception &e) {
			panic(string_format("uncaught exception: %s", e.what()).c_str());
		}
	}

	if (!*returns) {
		/* if the block ensured that all code paths returned, then the lifetimes
		 * of the related objects was managed. otherwise, let's do it here. */
		life->release_vars(builder, scope, lf_block);
	}

	if (expected_type != type_bottom()) {
		assert(block_value != nullptr);
		return block_value;
	} else {
		/* we should be checking for this */
        return nullptr;
	}
}

types::type_t::ref ast::block_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	assert(false);
	return nullptr;
}

bound_var_t::ref ast::expression_t::resolve_condition(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref block_scope,
		life_t::ref life,
		types::type_t::ref expected_type,
		runnable_scope_t::ref *,
		runnable_scope_t::ref *) const
{
	return resolve_expression(builder, block_scope, life, false /*as_ref*/,
			expected_type);
}

void ast::while_block_t::resolve_statement(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		runnable_scope_t::ref *,
		bool *returns) const
{
	/* while scope allows us to set up new variables inside while conditions */
	runnable_scope_t::ref while_scope;

	assert(token.text == "while" || token.text == "for");

	llvm::Function *llvm_function_current = llvm_get_function(builder);

	llvm::BasicBlock *while_cond_bb = llvm::BasicBlock::Create(builder.getContext(), "while.cond", llvm_function_current);

	assert(!builder.GetInsertBlock()->getTerminator());
	builder.CreateBr(while_cond_bb);
	builder.SetInsertPoint(while_cond_bb);

	/* demarcate a loop boundary here */
	life = life->new_life(lf_loop|lf_block);

	life_t::ref cond_life = life->new_life(lf_statement);
	bound_var_t::ref condition_value;

	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);

	/* evaluate the condition for branching */
	/* our user is attempting any assorted collection of ergonomic improvements to their life by
	 * asserting possible type modifications to their variables, or by injecting new variables
	 * into the nested scope. */
	condition_value = condition->resolve_condition(
			builder, runnable_scope, cond_life,
		   	type_id(make_iid(BOOL_TYPE)),
		   	&while_scope, nullptr /*scope_if_false*/);

	/* generate some new blocks */
	llvm::BasicBlock *while_block_bb = llvm::BasicBlock::Create(builder.getContext(), "while.block", llvm_function_current);
	llvm::BasicBlock *while_end_bb = llvm::BasicBlock::Create(builder.getContext(), "while.end");

	/* keep track of the "break" and "continue" jump locations */
	loop_tracker_t loop_tracker(dyncast<runnable_scope_t>(scope), while_cond_bb, while_end_bb);

	/* we don't have an else block, so we can just continue on */
	llvm_create_if_branch(builder, scope,
			IFF_ELSE, cond_life, condition->get_location(), condition_value,
			false /*allow_maybe_check*/,
			while_block_bb, while_end_bb);

	assert(builder.GetInsertBlock()->getTerminator());

	/* let's generate code for the "then" block */
	builder.SetInsertPoint(while_block_bb);
	assert(!builder.GetInsertBlock()->getTerminator());

	cond_life->release_vars(builder, scope, lf_statement);

	block->resolve_statement(builder,
			while_scope ? while_scope : scope, life, nullptr,
			nullptr);

	/* the loop can't store values */
	assert(life->values.size() == 0 && life->life_form == lf_loop);

	if (!builder.GetInsertBlock()->getTerminator()) {
		builder.CreateBr(while_cond_bb);
	}
	builder.SetInsertPoint(while_end_bb);

	/* we know we'll need to fall through to the merge
	 * block, let's add it to the end of the function
	 * and let's set it as the next insert point. */
	llvm_function_current->getBasicBlockList().push_back(while_end_bb);
	builder.SetInsertPoint(while_end_bb);
}

void ast::if_block_t::resolve_statement(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
        runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	assert(life->life_form == lf_statement);

	/* if scope allows us to set up new variables inside if conditions */
	runnable_scope_t::ref scope_if_true, scope_if_false;

	bool if_block_returns = false, else_block_returns = false;

	assert(token.text == "if" || token.text == "elif" || token.text == "assert");
	bound_var_t::ref condition_value;

	auto cond_life = life->new_life(lf_statement);

	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);

	/* evaluate the condition for branching */
	condition_value = condition->resolve_condition(
			builder, runnable_scope, cond_life,
		   	type_id(make_iid(BOOL_TYPE)),
		   	&scope_if_true, &scope_if_false);

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
	llvm_create_if_branch(builder, scope, IFF_ELSE, cond_life,
			condition->get_location(), condition_value, false /*allow_maybe_check*/,
			then_bb, else_bb);

	/* let's generate code for the "then" block */
	builder.SetInsertPoint(then_bb);
	cond_life->release_vars(builder, scope, lf_statement);

	block->resolve_statement(builder,
			scope_if_true ? scope_if_true : scope, life, nullptr, &if_block_returns);

	if (!if_block_returns) {
		insert_merge_bb = true;
		if (!builder.GetInsertBlock()->getTerminator()) {
			builder.CreateBr(merge_bb);
		}
		builder.SetInsertPoint(merge_bb);
	}

	builder.SetInsertPoint(else_bb);

	if (else_ != nullptr) {
		runnable_scope_t::ref scope_if_else_false;
		else_->resolve_statement(builder,
				scope_if_false ? scope_if_false : scope,
				life, &scope_if_else_false, &else_block_returns);

		if (scope_if_else_false != nullptr) {
			assert(!else_block_returns);
			/* if the false path is pushing new symbol refinements, then let's accept
			 * them because theoretically we don't care what that path is doing, if it
			 * knows more about our outer environment, then let's take that knowledge */
			scope_if_false = scope_if_else_false;
		}
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

	if (insert_merge_bb) {
		/* we know we'll need to fall through to the merge
		 * block, let's add it to the end of the function
		 * and let's set it as the next insert point. */
		llvm_function_current->getBasicBlockList().push_back(merge_bb);
		builder.SetInsertPoint(merge_bb);

	}

	/* track whether the branches return */
	*returns |= (if_block_returns && else_block_returns);

	if (!if_block_returns && else_block_returns) {
		*new_scope = scope_if_true;
	} else if (if_block_returns && !else_block_returns) {
		*new_scope = scope_if_false;
	}
}

bound_var_t::ref ast::bang_expr_t::resolve_expression(
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	auto lhs_value = lhs->resolve_expression(builder, scope, life,
			false /*as_ref*/, nullptr);

	auto type = lhs_value->type->get_type();
	auto maybe_type = dyncast<const types::type_maybe_t>(type);
	if (maybe_type != nullptr) {
		bound_type_t::ref just_bound_type = upsert_bound_type(
				builder, scope, maybe_type->just);
		return bound_var_t::create(INTERNAL_LOC(), lhs_value->name,
				just_bound_type,
				lhs_value->get_llvm_value(),
				lhs_value->id);
	} else {
		throw user_error(get_location(), "bang expression is unnecessary since this is not a 'maybe' type: %s",
				type->str().c_str());
	}
}

bound_var_t::ref ast::var_decl_t::resolve_as_link(
		llvm::IRBuilder<> &builder,
		module_scope_t::ref module_scope)
{
	if (initializer != nullptr) {
		throw user_error(get_location(), "linked variables cannot have initializers");
	}

	types::type_t::ref declared_type = get_type()->rebind(module_scope->get_type_variable_bindings());
	bound_type_t::ref var_type = upsert_bound_type(builder, module_scope, declared_type);
	bound_type_t::ref ref_var_type = upsert_bound_type(builder, module_scope, type_ref(declared_type));
	llvm::Module *llvm_module = module_scope->get_llvm_module();
	auto llvm_global_variable = new llvm::GlobalVariable(*llvm_module,
			var_type->get_llvm_specific_type(),
			false /*is_constant*/, llvm::GlobalValue::ExternalLinkage,
			nullptr, token.text, nullptr,
			llvm::GlobalVariable::NotThreadLocal);
	return bound_var_t::create(
			INTERNAL_LOC(),
			token.text,
			ref_var_type,
			llvm_global_variable,
			make_code_id(token));
}

bound_var_t::ref ast::var_decl_t::resolve_condition(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		types::type_t::ref expected_type,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false) const
{
	assert(false);
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
	assert(runnable_scope);

	/* variable declarations begin new scopes */
	runnable_scope_t::ref fresh_scope = runnable_scope->new_runnable_scope(
			string_format("condition-assignment-%s", token.text.c_str()));

	scope = fresh_scope;

	/* check to make sure this var decl is sound */
	bound_var_t::ref var_decl_value = type_check_bound_var_decl(
			builder, fresh_scope, *this, life, true /*maybe_unbox*/);

	*scope_if_true = fresh_scope;
	return var_decl_value;
}

void ast::var_decl_t::resolve_statement(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		runnable_scope_t::ref *new_scope,
		bool * /*returns*/) const
{
	if (auto runnable_scope = dyncast<runnable_scope_t>(scope)) {
		/* variable declarations begin new scopes */
		runnable_scope_t::ref fresh_scope = runnable_scope->new_runnable_scope(
				string_format("variable-%s", token.text.c_str()));

		scope = fresh_scope;

		/* check to make sure this var decl is sound */
		bound_var_t::ref var_decl_value = type_check_bound_var_decl(
				builder, fresh_scope, *this, life, false /*maybe_unbox*/);

			*new_scope = fresh_scope;
			return;
	} else {
		panic("we should not be trying to instantiate a var decl outside of a runnable scope");
		return;
	}
}


void ast::defer_t::resolve_statement(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		runnable_scope_t::ref *new_scope,
		bool *returns) const
{
	auto expr = callable->resolve_expression(builder,
			scope, life, false /*as_ref*/,
			type_deferred_function(
				get_location(),
			   	type_variable(get_location())));

	life->defer_call(builder, scope, expr);
}

bound_var_t::ref take_address(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		ast::expression_t::ref expr,
		types::type_t::ref expected_type)
{
	if (expected_type != nullptr) {
		if (auto ptr_type = dyncast<const types::type_ptr_t>(expected_type->eval(scope))) {
			expected_type = ptr_type->element_type;
		} else {
			expected_type = nullptr;
		}
	}

	/* first solve the right hand side */
	bound_var_t::ref rhs_var = expr->resolve_expression(builder,
			scope, life, true /*as_ref*/, expected_type);

	if (auto ref_type = dyncast<const types::type_ref_t>(rhs_var->type->get_type())) {
		bound_type_t::ref bound_ptr_type = upsert_bound_type(builder, scope, type_ptr(ref_type->element_type));
		return bound_var_t::create(
				expr->get_location(), string_format("address_of.%s", rhs_var->name.c_str()),
				bound_ptr_type, rhs_var->get_llvm_value(),
				make_code_id(expr->token));
	} else {
		throw user_error(expr->get_location(), "can't take address of %s", expr->str().c_str());
	}
}

bound_var_t::ref ast::prefix_expr_t::resolve_condition(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		types::type_t::ref expected_type,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false) const
{
	return resolve_prefix_expr(builder, scope, 
			life, false /*as_ref*/,
			expected_type,
			scope_if_true,
			scope_if_false);
}

bound_var_t::ref ast::prefix_expr_t::resolve_expression(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
	return resolve_prefix_expr(builder, runnable_scope, life, as_ref, expected_type, nullptr, nullptr);
}

bound_var_t::ref ast::prefix_expr_t::resolve_prefix_expr(
        llvm::IRBuilder<> &builder,
        runnable_scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false) const
{
	std::string function_name;
	switch (token.tk) {
	case tk_minus:
		function_name = "__negative__";
		break;
	case tk_plus:
		function_name = "__positive__";
		break;
	case tk_ampersand:
		return take_address(builder, scope, life, rhs, expected_type);
	case tk_identifier:
		if (token.is_ident(K(not))) {
			function_name = "__not__";
			break;
		}
	default:
		return null_impl();
	}

	/* first solve the right hand side */
	bound_var_t::ref rhs_var = rhs->resolve_condition(builder,
			scope, life, expected_type, scope_if_true, scope_if_false);

	if (function_name == "__not__") {
		bool is_managed;
		rhs_var->type->is_managed_ptr(builder, scope, is_managed);
		if (!is_managed) {
			if ((scope_if_true && *scope_if_true) || (scope_if_false && *scope_if_false)) {
				std::swap(*scope_if_true, *scope_if_false);
				runnable_scope_t::ref a, b;
				return resolve_null_check(builder, scope, life,
						get_location(), rhs, rhs_var, nck_is_null,
						&a, &b);
			} else {
				return resolve_null_check(builder, scope, life,
						get_location(), rhs, rhs_var, nck_is_null,
						scope_if_true, scope_if_false);
			}
		}
	}
	return call_module_function(builder, scope, life,
			function_name, get_location(), {rhs_var});
}

bound_var_t::ref ast::literal_expr_t::resolve_expression(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
    scope_t::ref program_scope = scope->get_program_scope();
	switch (token.tk) {
	case tk_identifier:
		{
			assert(token.text == "null");
			return get_null(builder, scope, token.location);
		}
		break;
	case tk_integer:
		{
			/* create a native integer */
			int64_t value = parse_int_value(token);
			unsigned bit_size = DEFAULT_INT_BITSIZE;
			bool signed_ = DEFAULT_INT_SIGNED;
			bound_type_t::ref native_type;

			if (expected_type != nullptr && expected_type->ftv_count() == 0 &&
				maybe_get_integer_attributes(
						token.location,
						expected_type,
						scope,
						bit_size,
						signed_))
			{
				native_type = upsert_bound_type(
						builder, program_scope,
						expected_type);
			} else {
				native_type = upsert_bound_type(
						builder, program_scope,
						type_id(make_iid(INT_TYPE)));
			}
			return bound_var_t::create(
					INTERNAL_LOC(), "int_literal", native_type,
					builder.getIntN(bit_size, value),
					make_code_id(token));
		}

	case tk_string:
		debug_above(8, log_location(log_info, token.location, "creating string: %s", token.text.c_str()));
		return create_global_str(builder, scope, token.location, unescape_json_quotes(token.text));

	case tk_float:
		{
			bound_type_t::ref native_type = upsert_bound_type(builder, program_scope, type_id(make_iid(FLOAT_TYPE)));
			double value = atof(token.text.c_str());
			return bound_var_t::create(
					INTERNAL_LOC(), "float_literal", native_type,
					llvm_create_double(builder, value),
					make_code_id(token));
		}
	default:
		assert(false);
		return nullptr;
	};
}

bound_var_t::ref ast::function_defn_t::resolve_overrides(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life_outer,
		const ptr<const ast::item_t> &callsite,
		const bound_type_t::refs &args,
		types::type_t::ref expected_type) const
{
    auto runnable_scope = dyncast<runnable_scope_t>(scope);
    assert(runnable_scope != nullptr);

    types::type_t::ref type_declared_fn = decl->function_type->rebind(scope->get_type_variable_bindings());
    debug_above(8, log("resolving overrides for function defn %s", type_declared_fn->str().c_str()));
    types::type_function_closure_t::ref closure_type = dyncast<const types::type_function_closure_t>(type_declared_fn);
    assert(closure_type != nullptr);

    types::type_t::refs param_types;
    for (auto arg : args) {
        param_types.push_back(arg->get_type());
    }
    /* ok, now because we know how we're being called, let's unify the declared type against the
     * inbound args and the expected type */
    unification_t unification = unify(
            closure_type,
            type_function_closure(
                type_function(INTERNAL_LOC(),
                    nullptr,
                    type_args(param_types, {}),
                    expected_type ? expected_type : type_variable(INTERNAL_LOC()))),
            scope);
    if (unification.result) {
        closure_type = dyncast<const types::type_function_closure_t>(closure_type->rebind(unification.bindings));
        types::type_function_t::ref fn_type = dyncast<const types::type_function_t>(closure_type->function);

        /* lifetimes have extents at function boundaries */
        auto life = make_ptr<life_t>(lf_function);

        auto closure_name = std::string("anonymous fn ") + fn_type->repr() + " at " + token.location.repr();
        auto closure_scope = runnable_scope->new_closure_scope(builder, closure_name);

        types::type_t::ref type_constraints;
        bound_type_t::named_pairs named_args;
        bound_type_t::ref return_type;
        bool needs_type_fixup = false;
        destructure_function_details(
                builder,
                closure_scope,
                callsite->get_location(),
                true /*as_closure*/,
                fn_type,
                needs_type_fixup,
                type_constraints,
                named_args,
                return_type);
        auto function = instantiate_function_with_args_and_return_type(
                builder,
                closure_scope,
                life,
                callsite->token,
                true /*as_closure*/,
                needs_type_fixup,
                nullptr /*extends_module*/,
                nullptr /*new_scope*/,
                type_constraints,
                named_args,
                return_type,
                fn_type,
                block);

        return closure_scope->create_closure(builder, life_outer, get_location(), function);
    } else {
        auto error = user_error(callsite->token.location, "callsite incompatible with function definition");
        error.add_info(callsite->token.location, "%s", unification.reasons.c_str());
        throw error;
    }

    assert(false);
    return nullptr;
}

bound_var_t::ref ast::reference_expr_t::resolve_overrides(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		const ptr<const ast::item_t> &callsite,
		const bound_type_t::refs &args,
		types::type_t::ref expected_type) const
{
	/* ok, we know we've got some variable here */
	try {
		return get_callable(builder, scope, token.text, get_location(), get_args_type(args), expected_type);
	} catch (user_error &e) {
		std::throw_with_nested(user_error(log_info, callsite->get_location(), "while checking %s with %s -> %s",
					callsite->str().c_str(),
					::str(args).c_str(),
					expected_type != nullptr ? expected_type->str().c_str() : c_type("any")));
	}

	return nullptr;
}

bound_var_t::ref ast::cast_expr_t::resolve_expression(
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		types::type_t::ref expected_type) const
{
	/* throw away expected type because we are saying we know what's best here */
	debug_above(8, log("attempting a cast to type %s in scope %s with bindings %s",
				type_cast->str().c_str(),
				scope->get_name().c_str(),
			   	::str(scope->get_type_variable_bindings()).c_str()));
	expected_type = type_cast->rebind(scope->get_type_variable_bindings())->eval(scope);

	if (!force_cast) {
		bound_var_t::ref bound_var = lhs->resolve_expression(builder, scope, life, false /*as_ref*/,
				expected_type);
		if (!unifies(expected_type, bound_var->type->get_type(), scope)) {
			throw user_error(lhs->get_location(), "unable to get a %s from this expression (which is of type %s)",
					expected_type->str().c_str(),
					bound_var->type->get_type()->str().c_str());
		}
		return coerce_bound_value(
				builder,
				scope,
				life,
				token.location,
				expected_type,
				bound_var);
	} else {
		bound_var_t::ref bound_var = lhs->resolve_expression(builder, scope, life, false /*as_ref*/,
				nullptr);

		return cast_bound_var(
				builder,
				scope,
				life,
				get_location(),
				bound_var,
				expected_type,
				force_cast);
	}
}

void dump_builder(llvm::IRBuilder<> &builder) {
	std::cerr << llvm_print_function(llvm_get_function(builder)) << std::endl;
}

types::type_t::ref ast::typeid_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	debug_above(6, log_location(log_info, get_location(), "typeid expr type resolution not yet impl"));
	return nullptr;
}

types::type_t::ref ast::sizeof_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	return type_id(make_iid("size_t"));
}

types::type_t::ref ast::callsite_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	debug_above(6, log_location(log_info, get_location(), "callsite type resolution not yet impl (would check %s with expected type %s)",
				str().c_str(),
				expected_type ? expected_type->str().c_str() : "<null>"));

#if 0
	if (auto ref_expr = dyncast<const ast::reference_expr_t>(function_expr)) {
		/* if we are calling a function by name, we should be able to work out the shape of the types for the parameters
		 * first, if we can resolve the function up front. here's what we know (without more digging):
		 * 1. arity
		 * 2. name
		 * */
		auto symbol = ref_expr->token.text;
		var_t::refs fns;
		scope->get_callables(symbol, fns);
		debug_above(6, log_location(log_info, ref_expr->token.location, "found possible callables %s", ::str(fns).c_str()));
		var_t::refs fns_arity;
		for (auto fn : fns) {
			auto fn_type = types::without_closure(fn->get_type(scope));
			if (fn_type != nullptr) {
				if (auto args = dyncast<const types::type_args_t>(fn_type->args)) {
					if (args->args.size() == params.size()) {
						fns_arity.push_back(fn
				if (args != 

	}
#endif

	return nullptr;
}

types::type_t::ref ast::cast_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	return expected_type;
}

types::type_t::ref ast::function_defn_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	return decl->function_type->rebind(scope->get_type_variable_bindings());
}

types::type_t::ref ast::link_function_statement_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	return nullptr;
}

types::type_t::ref ast::link_var_statement_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	return nullptr;
}

types::type_t::ref ast::dot_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	debug_above(6, log_location(log_info, get_location(), "dot expr type resolution not yet impl"));
	return nullptr;
}

types::type_t::ref ast::tuple_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	debug_above(6, log_location(log_info, get_location(), "tuple type resolution not yet impl"));
	return nullptr;
}

types::type_t::ref ast::ternary_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	debug_above(6, log_location(log_info, get_location(), "ternary type resolution not yet impl"));
	return nullptr;
}

types::type_t::ref ast::or_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	debug_above(6, log_location(log_info, get_location(), "or expr type resolution not yet impl"));
	return nullptr;
}

types::type_t::ref ast::and_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	debug_above(6, log_location(log_info, get_location(), "and expr type resolution not yet impl"));
	return nullptr;
}

types::type_t::ref ast::binary_operator_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	debug_above(6, log_location(log_info, get_location(), "binary operator type resolution not yet impl"));
	return nullptr;
}

types::type_t::ref ast::prefix_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	debug_above(6, log_location(log_info, get_location(), "prefix expr type resolution not yet impl"));
	return nullptr;
}

types::type_t::ref ast::typeinfo_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	debug_above(6, log_location(log_info, get_location(), "typeinfo type resolution not yet impl"));
	return nullptr;
}

types::type_t::ref ast::reference_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	return scope->get_variable_type(token.location, token.text, nullptr);
}

types::type_t::ref ast::literal_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
    scope_t::ref program_scope = scope->get_program_scope();
	switch (token.tk) {
	case tk_identifier:
		{
			assert(token.text == "null");
			return program_scope->get_bound_type("null")->get_type();
		}
		break;
	case tk_integer:
		{
			unsigned bit_size = DEFAULT_INT_BITSIZE;
			bool signed_ = DEFAULT_INT_SIGNED;
			if (expected_type != nullptr && expected_type->ftv_count() == 0 &&
				maybe_get_integer_attributes(
						token.location,
						expected_type,
						scope,
						bit_size,
						signed_))
			{
				return expected_type;
			} else {
				return type_id(make_iid(INT_TYPE));
			}
		}

	case tk_string:
		return type_id(make_iid_impl(MANAGED_STR, token.location));

	case tk_float:
		return type_id(make_iid(FLOAT_TYPE));

	default:
		assert(false);
		return nullptr;
	};
}

types::type_t::ref ast::array_literal_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	debug_above(6, log_location(log_info, get_location(), "array_literal_expr_t resolution not yet impl"));
	if (items.size() != 0) {
		auto item_type = items[0]->resolve_type(scope, nullptr);
		item_type = item_type ? item_type : type_variable(INTERNAL_LOC());
		return type_operator(type_id(make_iid(STD_VECTOR_TYPE)), item_type);
	}

	return nullptr;
}

types::type_t::ref ast::bang_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	debug_above(6, log_location(log_info, get_location(), "resolve type for array_index_expr not yet impl"));
	return nullptr;
}

types::type_t::ref ast::array_index_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	debug_above(6, log_location(log_info, get_location(), "resolve type for array_index_expr not yet impl"));
	return nullptr;
}

types::type_function_t::ref ast::function_defn_t::resolve_arg_types_from_overrides(
		scope_t::ref scope,
		location_t location,
		types::type_t::refs args,
		types::type_t::ref return_type) const
{
	debug_above(6, log_location(log_info, get_location(), "resolve_arg_types_from_overrides for function_defn_t not yet impl"));
	return nullptr;
}

types::type_function_t::ref ast::dot_expr_t::resolve_arg_types_from_overrides(
		scope_t::ref scope,
		location_t location,
		types::type_t::refs args,
		types::type_t::ref return_type) const
{
	debug_above(6, log_location(log_info, get_location(), "resolve_arg_types_from_overrides for dot_expr_t not yet impl"));
	return nullptr;
}

types::type_function_t::ref ast::reference_expr_t::resolve_arg_types_from_overrides(
		scope_t::ref scope,
		location_t location,
		types::type_t::refs arguments,
		types::type_t::ref return_type) const
{
	var_t::refs fns;
	scope->get_callables(token.text, fns, true /*check_unchecked*/);

	types::type_args_t::ref args = type_args(types::without_refs(arguments), {});
	types::type_function_t::ref final_fn_type;

	std::vector<types::type_function_t::ref> choices;
	for (auto fn : fns) {
		types::type_function_t::ref fn_type = check_func_type_vs_callsite(
				scope, location, fn, args, return_type);

		if (fn_type != nullptr) {
			/* we are optimistic because in the event of ambiguity between generics, and the like,
			 * we'd have failed here anyway */
			choices.push_back(fn_type);
		}
	}

	for (auto choice : choices) {
		debug_above(4, log_location(log_info, get_location(),
				"%s might be type %s when probed with args %s and return type %s",
				str().c_str(),
				choice->str().c_str(),
				args->str().c_str(),
				return_type ? return_type->str().c_str() : "<null>"));
	}

	if (choices.size() == 1) {
		return choices[0];
	} else if (choices.size() == 0) {
		return nullptr;
	} else {
		/* find the best candidate */
		for (size_t i = 0; i < choices.size(); i++) {
			if (choices[i] == nullptr) {
				continue;
			}

			for (size_t j = i + 1; j < choices.size(); j++) {
				if (choices[j] == nullptr) {
					continue;
				}

				if (choices[i]->repr() == choices[j]->repr()) {
					/* they are the same, just eliminate one of them */
					choices[j] = nullptr;
				} else if (choices[i]->get_location() == choices[j]->get_location()) {
					/* see if we can eliminate one of these */
					int ftv_diff = choices[i]->ftv_count() - choices[j]->ftv_count();
					if (ftv_diff > 0) {
						choices[j] = nullptr;
					} else if (ftv_diff < 0) {
						choices[i] = nullptr;
						break;
					} else {
						/* can't decide which is better */
						return nullptr;
					}
				}
			}
		}
		std::vector<types::type_function_t::ref> final_choices;
		std::copy_if (
				choices.begin(),
				choices.end(),
				std::back_inserter(final_choices),
				[](types::type_t::ref t) {
				return t != nullptr;
				});

		if (final_choices.size() == 1) {
			return final_choices[0];
		} else {
			return nullptr;
		}
	}
}
