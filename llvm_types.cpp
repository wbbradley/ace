#include "zion.h"
#include "ast.h"
#include "compiler.h"
#include "llvm_utils.h"
#include "llvm_types.h"
#include "type_visitor.h"
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

struct bound_type_builder_t : public types::type_visitor {
	bound_type_builder_t(
			status_t &status,
			llvm::IRBuilder<> &builder,
			ptr<scope_t> scope,
			types::term::map env) :
		status(status),
		builder(builder),
		scope(scope),
		program_scope(scope->get_program_scope()),
		env(env)
	{
	}

	status_t &status;
	llvm::IRBuilder<> &builder;
	bound_type_t::ref created_type;
	ptr<scope_t> scope;
	ptr<program_scope_t> program_scope;
	types::term::map env;

	virtual bool visit(const types::type_id &id) {
		created_type = scope->get_bound_type(id.get_signature());
		if (created_type == nullptr) {
			return false;
		}
		return created_type != nullptr;
	}

	virtual bool visit(const types::type_variable &variable) {
		user_error(status, variable.get_location(), "unable to resolve type %s",
			   	variable.str().c_str());
		return false;
	}

	virtual bool visit(const types::type_operator &operator_) {
		/* figure out where this type operator came from or what it means */
		auto module_scope = scope->get_module_scope();
		if (module_scope != nullptr) {
			atom name = operator_.oper->get_signature();

			auto type_decl_env = scope->get_type_env();
			assert(!in(name, type_decl_env));
			return false;
		} else {
			assert(!"no module scope?");
		}
		return !!status;
	}

	virtual bool visit(const types::type_product &product) {
		switch (product.pk) {
		case pk_obj:
			{
				assert(false);
				break;
			}
		case pk_function:
			{
				assert(product.dimensions.size() == 2);
				bound_type_t::refs args = create_bound_types_from_args(status,
						builder, scope, product.dimensions[0]);
				bound_type_t::ref return_type = upsert_bound_type(
						status, builder, scope, product.dimensions[1]);

				if (!!status) {
					types::type::ref fn_type = get_function_type(args, return_type);

					auto signature = fn_type->get_signature();
					created_type = scope->get_bound_type(signature);
					if (created_type) {
						return true;
					} else {
						auto *llvm_fn_type = llvm_create_function_type(status,
								builder, args, return_type);
						if (!!status) {
							created_type = bound_type_t::create(fn_type,
									product.get_location(), llvm_fn_type);
							program_scope->put_bound_type(created_type);
						}
						return !!status;
					}
				}
				return !!status;
			}
		case pk_args:
			{
				assert(false);
				break;
			}
		case pk_tuple:
			{
				assert(false);
				break;
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
		case pk_named_dimension:
			{
				assert(false);
				break;
			}
		}
		assert(false);
		return false;
	}

	virtual bool visit(const types::type_sum &sum) {
		dbg();
		auto signature = sum.get_signature();
		auto bound_type = scope->get_bound_type(signature);
		assert(bound_type == nullptr);
		created_type = bound_type_t::create(sum.shared_from_this(),
				sum.get_location(),
				scope->get_bound_type({"__var_ref"})->get_llvm_type());
		program_scope->put_bound_type(created_type);
		return true;
	}
};

bound_type_t::ref create_bound_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type::ref type)
{
	assert(!!status);
	auto env = scope->get_type_env();
	auto term = type->to_term();
	debug_above(6, log(log_info, "evaluating %s in %s",
				term->str().c_str(),
				::str(env).c_str()));
	auto final_type = term->evaluate(env)->get_type(status);
	debug_above(6, log(log_info, "evaluated %s to %s",
				term->str().c_str(),
				final_type->str().c_str()));
	if (!!status) {
		/* short-circuit for the case that we've built a term_binder and the
		 * very act of getting a type for a term will instantiate a type. */
		auto bound_type = scope->get_bound_type(final_type->get_signature());
		if (bound_type) {
			return bound_type;
		}
	}

	indent_logger indent(3,
		string_format("creating bound type for %s in env %s",
			type->str().c_str(), str(env).c_str()));

	bound_type_builder_t btb(status, builder, scope, env);
	if (type->accept(btb)) {
		assert(!!status);
		return btb.created_type;
	} else {
		user_error(status, type->get_location(), "unable to find a definition for %s",
				type->str().c_str());
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
		return create_bound_type(status, builder, scope, type);
	}
}

bound_type_t::ref upsert_bound_type(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
	   	types::term::ref term)
{
	/* helper method to convert lambda terms to types */
	debug_above(6, log(log_info, "evaluating type term " c_term("%s"),
				term->str().c_str()));
	auto type_env = scope->get_type_env();
	auto type = term->evaluate(type_env)->get_type(status);

	if (!!status) {
		return upsert_bound_type(status, builder, scope, type);
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

	/* get the term of this tuple type */
	types::term::ref term = get_obj_term(get_tuple_term(get_terms(args)));
	types::type::ref type = term->get_type(status);
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
		const ast::item::ref &node,
		types::type::ref type)
{
	assert(type != nullptr);

	debug_above(5, log(log_info, "get_or_create_algebraic_data_type looking for %s",
			type->get_signature().c_str()));

	bound_type_t::ref data_type = scope->get_bound_type(type->get_signature());

	if (data_type != nullptr) {
		return data_type;
	} else {
		return create_algebraic_data_type(builder, scope, id, args,
				member_index, node, type);
	}
}

bound_type_t::ref create_algebraic_data_type(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		identifier::ref id,
		bound_type_t::refs args,
		atom::map<int> member_index,
		const ast::item::ref &node,
		types::type::ref type)
{
	assert(id != nullptr);
	assert(node != nullptr);

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
			node->token.location,
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
				scope, id, args, member_index, node, type);

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
	debug_above(5, log(log_info, "creating type alias term for %s", str().c_str()));

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
