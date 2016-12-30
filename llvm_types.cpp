#include "zion.h"
#include "ast.h"
#include "compiler.h"
#include "llvm_utils.h"
#include "llvm_types.h"
#include "code_id.h"
#include "logger.h"

bound_type_t::refs create_bound_types_from_args(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type::ref args_type)
{
	/* iteratate over a pk_args type and pull out a list of the bound types
	 * within */
	if (auto product = dyncast<const types::type_product>(args_type)) {
		assert(product->pk == pk_args);
		bound_type_t::refs args;
		for (auto dimension : product->dimensions) {
			bound_type_t::ref arg = upsert_bound_type(status, builder, scope,
					dimension);
			if (!!status) {
				args.push_back(arg);
			}
		}
		return args;
	} else {
		panic("do not call create_bound_types_from_args on a non-product type");
		return {};
	}
}

bound_type_t::ref create_bound_product_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_product> &product)
{
	ptr<program_scope_t> program_scope = scope->get_program_scope();

	switch (product->pk) {
	case pk_obj:
		{
			assert(false);
			break;
		}
	case pk_function:
		{
			assert(product->dimensions.size() == 2);
			bound_type_t::refs args = create_bound_types_from_args(status,
					builder, scope, product->dimensions[0]);
			bound_type_t::ref return_type = upsert_bound_type(
					status, builder, scope, product->dimensions[1]);

			if (!!status) {
				types::type::ref fn_type = get_function_type(args, return_type);
				assert(fn_type->str() == product->str());

				auto signature = fn_type->get_signature();
				auto bound_type = scope->get_bound_type(signature);
				if (bound_type) {
					return bound_type;
				} else {
					auto *llvm_fn_type = llvm_create_function_type(status,
							builder, args, return_type);
					if (!!status) {
						bound_type = bound_type_t::create(fn_type,
								product->get_location(), llvm_fn_type);
						program_scope->put_bound_type(bound_type);
						return bound_type;
					}
				}
			}
			break;
		}
	case pk_args:
		{
			assert(false);
			break;
		}
	case pk_tuple:
		{
			if (product->ftv_count() != 0) {
				return program_scope->get_bound_type({BUILTIN_UNREACHABLE_TYPE});
			}

			/* start by registering a placeholder handle for the data ctor's
			 * actual final type */
			assert(!scope->get_bound_type(product->get_signature()));
			auto bound_type_handle = bound_type_t::create_handle(
					product,
					program_scope->get_bound_type({"__var_ref"})->get_llvm_type());
			program_scope->put_bound_type(bound_type_handle);

			bound_type_t::refs args;
			for (auto dim : product->dimensions) {
				auto arg = upsert_bound_type(status, builder, scope, dim);

				if (!status) {
					break;
				}
				args.push_back(arg);
			}

			if (!!status) {
				auto bound_type = create_algebraic_data_type(builder, scope,
						types::gensym(), args, product->name_index,
						product->get_location(), product);
				bound_type_handle->set_actual(bound_type);
				return bound_type;
			}
		}
	case pk_tag:
		{
			assert(false);
			break;
		}
	case pk_tagged_tuple:
		{
			assert(false);
			break;
		}
	case pk_struct:
		{
			assert(false);
			break;
		}
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref create_bound_operator_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_operator> &operator_)
{
	debug_above(4, log(log_info, "create_bound_operator_type(..., %s)", operator_->str().c_str()));

	/* the strategy with operator types is to bind a handle for them, then
	 * expand them and recurse by creating all of their contained or subtypes.
	 * once we have an idea of that expansion's type, we set actual on that */

	/* apply the operator */
	auto expansion = eval_apply(operator_->oper, operator_->operand,
			scope->get_typename_env(), {});

	if (expansion == nullptr) {
		user_error(status, operator_->get_location(),
				"unable to find a definition for %s",
				operator_->str().c_str());
	} else {
		/* we've evaluated the application of this type operator. create a
		 * handle to track resolution of this type. */
		auto program_scope = scope->get_program_scope();
		auto bound_type_handle = bound_type_t::create_handle(
				operator_,
				program_scope->get_bound_type({"__var_ref"})->get_llvm_type());
		program_scope->put_bound_type(bound_type_handle);
	
		/* go ahead and recurse to resolve this new expanded type */
		bound_type_t::ref bound_expansion = upsert_bound_type(status, builder,
				scope, expansion);

		if (bound_expansion != nullptr) {
			/* we found the 'true' type for this type operator, let's map the
			 * type operator back to this new 'truth' */
			bound_type_handle->set_actual(bound_expansion);
			return bound_type_handle;
		} else {
			user_error(status, operator_->get_location(),
					"failed to bind concrete type to %s afer expansion to %s",
					operator_->str().c_str(),
					expansion->str().c_str());
		}
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref create_bound_sum_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_sum> &sum)
{
	assert(!scope->get_bound_type(sum->get_signature()));

	auto bound_type = bound_type_t::create(sum,
			sum->get_location(),
			scope->get_bound_type({"__var_ref"})->get_llvm_type());

	ptr<program_scope_t> program_scope = scope->get_program_scope();
	program_scope->put_bound_type(bound_type);
	return bound_type;
}

bound_type_t::ref create_bound_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type::ref type)
{
	assert(!!status);

	auto env = scope->get_typename_env();
	indent_logger indent(3,
		string_format("attempting to create a bound type for %s",
			type->str().c_str()));

	if (auto id = dyncast<const types::type_id>(type)) {
		/* right here, we know that this type does not have a bound type. so,
		 * let's go ahead and create a "handle" to prevent infinite recursion on
		 * this guy. */
		assert(!scope->get_bound_type(id->get_signature()));

		auto program_scope = scope->get_program_scope();
		auto bound_type_handle = bound_type_t::create_handle(
				id,
				program_scope->get_bound_type({"__var_ref"})->get_llvm_type());
		program_scope->put_bound_type(bound_type_handle);

		/* now, we can do whatever it takes to resolve this. let's look up this
		 * type in the environment to see if it resolves to any already known
		 * bound_types. this will likely involve recursion. */
		auto type_iter = env.find(id->get_id()->get_name());
		if (type_iter != env.end()) {
			auto type = type_iter->second;
			debug_above(2, log(log_info, "found unbound type_id in env " c_type("%s") " => %s",
						id->get_signature().c_str(),
						type->str().c_str()));

			if (auto lambda = dyncast<const types::type_lambda>(type)) {
				debug_above(4, log(log_info, "type_id %s expands to type_lambda %s",
							id->str().c_str(),
							lambda->str().c_str()));
				user_error(status, type->get_location(), "type %s resolves to a lambda, however we found a reference that does not supply parameters",
						type->str().c_str());
			} else {
				/* cool, we have a term we can recurse on. */
				auto bound_type = upsert_bound_type(
						status, builder, scope, type);

				if (!!status) {
					bound_type_handle->set_actual(bound_type);
					return bound_type_handle;
				}
			}
		} else {
			user_error(status, type->get_location(),
					"unable to find a type definition for %s",
					type->str().c_str());
		}

		assert(!status);
		return nullptr;
		
	} else if (auto product = dyncast<const types::type_product>(type)) {
		return create_bound_product_type(status, builder, scope, product);
	} else if (auto sum = dyncast<const types::type_sum>(type)) {
		return create_bound_sum_type(status, builder, scope, sum);
	} else if (auto operator_ = dyncast<const types::type_operator>(type)) {
		return create_bound_operator_type(status, builder, scope, operator_);
	} else if (auto variable = dyncast<const types::type_variable>(type)) {
		not_impl();
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref upsert_bound_type(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
	   	types::type::ref type)
{
	auto signature = type->get_signature();
	auto bound_type = scope->get_bound_type(signature);
	if (bound_type != nullptr) {
		return bound_type;
	} else {
		/* complete the binding of the type, just in case */
		auto desired_type = type->rebind(scope->get_type_variable_bindings());

		debug_above(6, log(log_info, "rebinding %s obtained %s",
					type->str().c_str(),
					desired_type->str().c_str()));

		bound_type = create_bound_type(status, builder, scope, desired_type);

		if (!!status) {
			return bound_type;
		}

		user_error(status, desired_type->get_location(),
			   	"unable to find a definition for %s",
				desired_type->str().c_str());
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref get_function_return_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ast::item &obj,
		scope_t::ref scope,
		bound_type_t::ref function_type)
{
	if (auto product_type = dyncast<const types::type_product>(function_type->get_type())) {
		assert(product_type->pk == pk_function);

		/* notice the leaky encapsulation here */
		assert(product_type->dimensions.size() == 2);

		auto return_type_sig = product_type->dimensions[1]->get_signature();

		auto return_type = scope->get_bound_type(return_type_sig);
		/* this should exist, otherwise how was the function type built in the
		 * first place */
		assert(return_type != nullptr);
		debug_above(8, log(log_info, "got function return type %s", return_type->str().c_str()));
		return return_type;
	} else {
		panic("expected a function");
		return nullptr;
	}
}

bound_type_t::ref get_or_create_tuple_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		identifier::ref id,
		bound_type_t::refs args,
		const ast::item::ref &node)
{
	atom name = id->get_name();

	/* get the type of this tuple type */
	types::type::ref type = get_obj_type(get_tuple_type(get_types(args)));
	auto data_type = scope->get_bound_type(type->get_signature());

	if (data_type != nullptr) {
		return data_type;
	} else {
		auto program_scope = scope->get_program_scope();

		/* build the llvm specific type */
		llvm::Type *llvm_tuple_type = llvm_create_tuple_type(
				builder, program_scope, name, args);

		/* display the new type */
		llvm::Type *llvm_obj_struct_type = llvm::cast<llvm::PointerType>(llvm_tuple_type)->getElementType();
		debug_above(5, log(log_info, "created LLVM wrapped tuple type %s", llvm_print_type(*llvm_obj_struct_type).c_str()));

		/* get the bound type of the data ctor's value */
		bound_type_t::ref data_type = bound_type_t::create(
				type,
				node->token.location,
				/* the LLVM-visible type of tuples will usually be a generic
				 * obj */
				scope->get_bound_type({"__var_ref"})->get_llvm_type(),
				llvm_tuple_type,
				args);

		/* put the type for the data type */
		program_scope->put_bound_type(data_type);

		return data_type;
	}
}

bound_type_t::ref get_or_create_algebraic_data_type(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		identifier::ref id,
		bound_type_t::refs args,
		atom::map<int> member_index,
		struct location location,
		types::type::ref type)
{
	// TODO: consider having this just call upsert_bound_type
	assert(type != nullptr);

	debug_above(5, log(log_info, "get_or_create_algebraic_data_type looking for %s",
			type->get_signature().c_str()));

	bound_type_t::ref data_type = scope->get_bound_type(type->get_signature());

	if (data_type != nullptr) {
		return data_type;
	} else {
		return create_algebraic_data_type(builder, scope, id, args,
				member_index, location, type);
	}
}

bound_type_t::ref create_algebraic_data_type(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		identifier::ref id,
		bound_type_t::refs args,
		atom::map<int> member_index,
		struct location location,
		types::type::ref type)
{
	assert(id != nullptr);

	auto program_scope = scope->get_program_scope();

	/* build the llvm return type */
	llvm::Type *llvm_tuple_type = llvm_create_tuple_type(
			builder, program_scope, id->get_name(), args);

	/* display the new type */
	llvm::Type *llvm_obj_struct_type = llvm::cast<llvm::PointerType>(llvm_tuple_type)->getElementType();
	debug_above(5, log(log_info, "created LLVM wrapped type %s", llvm_print_type(*llvm_obj_struct_type).c_str()));

	assert_implies(member_index.size() != 0, member_index.size() == args.size());

	/* get the bound type of the data ctor's value */
	auto data_type = bound_type_t::create(
			type,
			location,
			/* the LLVM-visible type of tagged tuples will usually be a
			 * generic obj */
			scope->get_bound_type({"__var_ref"})->get_llvm_type(),
			llvm_tuple_type,
			args,
			member_index);

	/* put the type for the data type. the scope can handle the case where the
	 * type already exists. */
	program_scope->put_bound_type(data_type);

	return data_type;
}

std::pair<bound_var_t::ref, bound_type_t::ref> instantiate_tuple_ctor(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::refs args,
		identifier::ref id,
		const ast::item::ref &node)
{
	/* this is a tuple constructor function */
	if (!!status) {
		program_scope_t::ref program_scope = scope->get_program_scope();

		bound_type_t::ref data_type = get_or_create_tuple_type(status, builder, scope,
				id, args, node);

		if (!!status) {
			bound_var_t::ref tuple_ctor = get_or_create_tuple_ctor(status, builder,
					scope, args, data_type, id, node);

			if (!!status) {
				return {tuple_ctor, data_type};
			}
		}
	}

	assert(!status);
	return {nullptr, nullptr};
}

std::pair<bound_var_t::ref, bound_type_t::ref> instantiate_tagged_tuple_ctor(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::refs args,
		atom::map<int> member_index,
		identifier::ref id,
		const ast::item::ref &node,
		types::type::ref type)
{
	assert(id != nullptr);
	assert(type != nullptr);

	/* this is a tuple constructor function */
	if (!!status) {
		program_scope_t::ref program_scope = scope->get_program_scope();

		bound_type_t::ref data_type = get_or_create_algebraic_data_type(builder,
				scope, id, args, member_index, node->token.location, type);

		bound_var_t::ref tagged_tuple_ctor = get_or_create_tuple_ctor(status, builder,
				scope, args, data_type, id, node);

		if (!!status) {
			return {tagged_tuple_ctor, data_type};
		}
	}

	assert(!status);
	return {nullptr, nullptr};
}

bound_var_t::ref get_or_create_tuple_ctor(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::refs args,
		bound_type_t::ref data_type,
		identifier::ref id,
		const ast::item::ref &node)
{
	atom name = id->get_name();

	auto program_scope = scope->get_program_scope();

	/* save and later restore the current branch insertion point */
	llvm::IRBuilderBase::InsertPointGuard ipg(builder);
	auto function = llvm_start_function(status, builder, scope,
			node, args, data_type, name);

	if (!!status) {
		bound_var_t::ref mem_alloc_var = program_scope->get_bound_variable(
				status, node, "__create_var");

		assert(!!status);
		assert(mem_alloc_var != nullptr);

		llvm::Value *llvm_sizeof_tuple = llvm_sizeof_type(builder, llvm_deref_type(data_type->get_llvm_type()));

		auto signature = get_function_return_type(function->type->get_type())->get_signature();
		debug_above(5, log(log_info, "mapping type " c_type("%s") " to typeid %d",
					signature.c_str(), signature.iatom));

		llvm::Value *llvm_create_var_call_value = llvm_create_call_inst(
				status, builder, *node,
				mem_alloc_var,
				{
					/* name this variable */
					builder.CreateGlobalStringPtr(name.str()),

					/* no mark function yet */
					llvm::Constant::getNullValue(
							program_scope->get_bound_type({"__mark_fn"})->get_llvm_type()),

					/* the type_id */
					builder.getInt32(signature.iatom),

					/* allocation size */
					llvm_sizeof_tuple
				});

		assert(data_type->get_llvm_specific_type() != nullptr);

		/* we've allocated enough space for the object type, let's get our allocation as such */
		llvm::Value *llvm_final_obj = builder.CreatePointerBitCastOrAddrSpaceCast(
				llvm_create_var_call_value, 
				data_type->get_llvm_specific_type());

		int index = 0;

		llvm::Function *llvm_function = (llvm::Function *)function->llvm_value;
		llvm::Function::arg_iterator args_iter = llvm_function->arg_begin();
		while (args_iter != llvm_function->arg_end()) {
			llvm::Value *llvm_param = args_iter++;
			/* get the location we should store this datapoint in */
			llvm::Value *llvm_gep = builder.CreateInBoundsGEP(llvm_final_obj,
					{builder.getInt32(0), builder.getInt32(1), builder.getInt32(index++)});
			debug_above(5, log(log_info, "store %s at %s", llvm_print_value(*llvm_param).c_str(),
					llvm_print_value(*llvm_gep).c_str()));
			builder.CreateStore(llvm_param, llvm_gep);
		}

		/* create a return statement for the final object. NB: the returned
		 * type is a generic object type according to LLVM. LLVM's type system
		 * does not support Zion types so we are basically using a generic obj
		 * pointer for all GC'd types. */
		builder.CreateRet(llvm_create_var_call_value);

		llvm_verify_function(status, llvm_function);

		if (!!status) {
			/* bind the ctor to the scope */
			scope->put_bound_variable(status, name, function);

			if (!!status) {
				debug_above(7, log(log_info, "module so far is:\n" c_ir("%s"), llvm_print_module(
								*llvm_get_module(builder)).c_str()));
				return function;
			}
		}
	}
	assert(!status);
	return nullptr;
}

void ast::type_alias::register_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		identifier::ref supertype_id,
		identifier::refs type_variables,
	   	scope_t::ref scope) const
{
	debug_above(5, log(log_info, "creating type alias for %s", str().c_str()));

	if (type_variables.size() != 0) {
		user_error(status, token.location, "found type variables in type alias - not yet impl");
	} else {
		user_error(status, token.location, "type aliasing is not yet impl");
	}
}

bound_var_t::ref type_check_get_item_with_int_literal(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		const ast::item::ref &node,
		bound_var_t::ref lhs,
		identifier::ref index_id,
		int subscript_index)
{
	if (!!status) {
		bound_var_t::ref index = bound_var_t::create(
				INTERNAL_LOC(),
				"temp_deref_index",
				scope->get_program_scope()->get_bound_type({INT_TYPE}),
				llvm_create_int(builder, subscript_index),
				index_id,
				false/*is_lhs*/);

		/* get or instantiate a function we can call on these arguments */
		return call_program_function(status, builder, scope, "__getitem__",
				node, {lhs, index});
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref call_const_subscript_operator(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		const ast::item::ref &node,
		bound_var_t::ref lhs,
		identifier::ref index_id,
		int subscript_index)
{
	debug_above(6, log(log_info, "generating dereference %s[%d]", lhs->str().c_str(), subscript_index));
	if (subscript_index < 0) {
		user_error(status, *node, "constant subscripts must be positive");
	} else {
		/* do some checks on the lhs */
		auto lhs_type = lhs->type;
		if (lhs_type->get_dimensions().size() != 0) {
			if (lhs_type->get_dimensions().size() > subscript_index) {
				/* ok, we're in range */
				debug_above(6, log(log_info, "generating dereference %s[%d]", lhs->str().c_str(), subscript_index));

				bound_type_t::ref data_type = lhs_type->get_dimensions()[subscript_index];
				assert(data_type != nullptr);
				assert(lhs_type->get_llvm_specific_type() != nullptr);

				/* get the tuple */
				llvm::Value *llvm_lhs = llvm_resolve_alloca(builder, lhs->llvm_value);

				llvm::Value *llvm_lhs_subtype = builder.CreatePointerBitCastOrAddrSpaceCast(
						llvm_lhs,
						lhs_type->get_llvm_specific_type());

				debug_above(5, log(log_info, "creating GEP for %s", llvm_print_value(*llvm_lhs_subtype).c_str()));
				llvm::Value *llvm_gep = builder.CreateInBoundsGEP(llvm_lhs_subtype,
						{builder.getInt32(0), builder.getInt32(1), builder.getInt32(subscript_index)});
				llvm::Value *llvm_value = builder.CreateLoad(llvm_gep);
				return bound_var_t::create(
						INTERNAL_LOC(),
						"temp_deref_subscript",
						data_type,
						llvm_value,
						make_code_id(node->token),
						false/*is_lhs*/);
			} else {
				user_error(status, *node, "index out of range");
			}
		} else {
			return type_check_get_item_with_int_literal(status, builder, scope,
					node, lhs, index_id, subscript_index);
		}
	}
	assert(!status);
	return nullptr;
}
