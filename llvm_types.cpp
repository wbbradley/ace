#include "zion.h"
#include "ast.h"
#include "compiler.h"
#include "llvm_utils.h"
#include "llvm_types.h"
#include "code_id.h"
#include "logger.h"
#include <iostream>

bound_type_t::refs upsert_bound_types(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type::refs types)
{
	/* iteratate over a product type and pull out a list of the bound types
	 * within */
	bound_type_t::refs bound_args;
	for (auto type : types) {
		bound_type_t::ref bound_arg = upsert_bound_type(status, builder, scope,
				type);
		if (!!status) {
			bound_args.push_back(bound_arg);
		} else {
			return {};
		}
	}
	return bound_args;
}

bound_type_t::ref create_ref_ptr_type(
		llvm::IRBuilder<> &builder,
		types::type_ref::ref ref_type)
{
	debug_above(4, log(log_info, "creating ref type for %s", ref_type->element_type->str().c_str()));
	llvm::StructType *llvm_type = llvm::StructType::create(
			builder.getContext(),
			ref_type->element_type->get_signature().str());
	assert(!llvm_type->isSized());
	assert(llvm_type->isOpaque());

	return bound_type_t::create(
			ref_type,
			ref_type->get_location(),
			llvm_type->getPointerTo());
}

bound_type_t::ref create_bound_ref_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_ref> &ref_type)
{
	ptr<program_scope_t> program_scope = scope->get_program_scope();

	assert(!scope->get_bound_type(ref_type->get_signature()));
	bound_type_t::ref bound_type = scope->get_bound_type(
			ref_type->element_type->get_signature());
	if (bound_type != nullptr) {
		return bound_type_t::create(ref_type,
				ref_type->get_location(),
				bound_type->get_llvm_type()->getPointerTo());
	}
				
	/* we've never seen the internal type, so start by registering an
	 * opaque placeholder for the struct's concrete type */
	
	/* first create the opaque pointer type */
	auto bound_pointer_type = create_ref_ptr_type(builder, ref_type);
	program_scope->put_bound_type(status, bound_pointer_type);

	assert(dyncast<const types::type_struct>(ref_type->element_type) != nullptr);
	
	/* before we return the pointer type, let's go ahead and instantiate
	 * the actual structural type */
	auto element = upsert_bound_type(status, builder, scope, ref_type->element_type);
	auto llvm_element_type = llvm::dyn_cast<llvm::StructType>(element->get_llvm_specific_type());
	assert(llvm_element_type != nullptr);
	assert(!llvm_element_type->isOpaque());

	if (!!status) {
		auto bound_element_type = scope->get_bound_type(ref_type->element_type->get_signature());
		assert(bound_element_type != nullptr);
		assert(bound_element_type->get_llvm_specific_type() == element->get_llvm_specific_type());

		return bound_pointer_type;
	}

	assert(!status);
	return nullptr;
}

std::vector<llvm::Type *> build_struct_elements(
		llvm::IRBuilder<> &builder,
	   	program_scope_t::ref program_scope,
	   	types::type_struct::ref struct_type,
		bound_type_t::refs bound_dimensions)
{
		/* create the structure in place in this struct type */
		std::vector<llvm::Type *> elements;
		if (struct_type->managed) {
			/* let's prefix the data in this structure with the managed runtime
			 * data */
			bound_type_t::ref var_type = program_scope->get_bound_type({"__var"});
			llvm::Type *llvm_var_type = var_type->get_llvm_type();

			/* place the var_t struct into the structure */
			elements.push_back(llvm_var_type);

			/* now place the logical data into the structure */
			elements.push_back(llvm_create_struct_type(
						builder, struct_type->get_signature(), bound_dimensions));
		} else {
			/* this is a native structure, let's just iterate over the bound
			 * dimensions and place those directly into this structure */
			for (auto bound_dimension : bound_dimensions) {
				elements.push_back(bound_dimension->get_llvm_type());
			}
		}

		assert(elements.size() != 0);
		assert_implies(struct_type->managed, elements.size() == 2);

		return elements;
}

bound_type_t::ref create_bound_struct_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_struct> &struct_type)
{
	ptr<program_scope_t> program_scope = scope->get_program_scope();

	if (struct_type->ftv_count() != 0) {
		debug_above(5, log(log_info,
					"found abstract type %s when attempting to create a bound type",
					struct_type->str().c_str()));
		return program_scope->get_bound_type({BUILTIN_UNREACHABLE_TYPE});
	}

	/* tuples don't have names, so there's no need for a placeholder, as
	 * they cannot be self referential */

	/* make sure one of these doesn't already exist */
	assert(!scope->get_bound_type(struct_type->get_signature()));

	/* get the pointer type to this, if it exists, get the opaque struct
	 * pointer that it had created. fill it out. if it doesn't exist,
	 * create it, then extract this tuple type from that. */
	types::type::ref ref_type = type_ref(struct_type);
	bound_type_t::ref bound_ref_type = scope->get_bound_type(
			ref_type->get_signature());

	if (ref_type != nullptr) {
		/* fetch the previously created pointer to this type */
		llvm::StructType *llvm_struct_type = llvm::dyn_cast<llvm::StructType>(llvm::cast<llvm::PointerType>(
					bound_ref_type->get_llvm_type())->getElementType());
		assert(llvm_struct_type != nullptr);
		assert(!llvm_struct_type->isSized());
		assert(llvm_struct_type->isOpaque());

		/* ensure that if this type is managed we refer to it generally by its
		 * managed structure definition (upwards pointer bitcasts happen
		 * automatically at reference locations) */
		llvm::Type *llvm_least_specific_type = (
				struct_type->managed
				? program_scope->get_bound_type({"__var"})->get_llvm_type()
				: llvm_struct_type);

		/* resolve all of the contained dimensions. NB: cycles should be broken
		 * by the existence of the pointer to this type */
		bound_type_t::refs bound_dimensions = upsert_bound_types(status,
				builder, scope, struct_type->dimensions);

		if (!!status) {
			/* fill out the internals of this structure */
			std::vector<llvm::Type *> elements = build_struct_elements(
					builder, program_scope, struct_type, bound_dimensions);

			/* finally set the elements into the structure */
			llvm_struct_type->setBody(elements);

			auto bound_type = bound_type_t::create(struct_type,
					struct_type->get_location(), llvm_least_specific_type,
					llvm_struct_type);

			/* register this type */
			program_scope->put_bound_type(status, bound_type);

			if (!!status) {
				return bound_type;
			}
		}
	} else {
		user_error(status, struct_type->get_location(),
				"cyclical type definition? %s",
				struct_type->str().c_str());
#if 0
		/* we created this type through recursion */
		auto bound_type = scope->get_bound_type(struct_type->get_signature());
		assert(bound_type != nullptr);
		return bound_type;
#endif
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref bind_type_expansion(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
	   	types::type::ref type,
		std::string struct_name)
{
	/* first create the opaque type */
	llvm::StructType *llvm_type = llvm::StructType::create(
			builder.getContext(), struct_name);
	assert(!llvm_type->isSized());
	assert(llvm_type->isOpaque());

	auto bound_type = bound_type_t::create(type, type->get_location(), llvm_type);
	auto program_scope = scope->get_program_scope();
	program_scope->put_bound_type(status, bound_type);

	if (!!status) {
		/* now, we can do whatever it takes to resolve this. let's look up this
		 * type in the environment to see if it resolves to any already known
		 * bound_types. this involves recursion on contained types. */
		if (type != nullptr) {
			debug_above(2, log(log_info, "found unbound type_id in env " c_type("%s") " => %s",
						type->get_signature().c_str(),
						type->str().c_str()));

			if (auto lambda = dyncast<const types::type_lambda>(type)) {
				debug_above(4, log(log_info, "type_id %s expands to type_lambda %s",
							type->str().c_str(),
							lambda->str().c_str()));
				user_error(status, type->get_location(),
						"type %s resolves to a lambda, however we found a reference that does not supply parameters",
						type->str().c_str());
			} else {
				/* cool, we have a term we can recurse on. */
				auto bound_type = upsert_bound_type(status, builder, scope, type);

				if (!!status) {
					llvm::StructType *llvm_struct_type = llvm::dyn_cast<llvm::StructType>(
							bound_type->get_llvm_type());

					if (llvm_struct_type != nullptr) {
						/* we're resolved what the structure looks like,
						 * let's set that as the structure's body */
						llvm_type->setBody(llvm_struct_type->elements());

						/* and we're done instantiating this type in LLVM */
						return bound_type;
					} else {
						user_error(status, type->get_location(),
								"failed to find a structure definition for %s",
								type->str().c_str());
						user_message(log_info, status, type->get_location(),
								"did find: %s %s",
								bound_type->str().c_str(),
								llvm_print_type(*bound_type->get_llvm_type()).c_str());
					}
				}
			}
		} else {
			user_error(status, type->get_location(),
					"unable to find a type definition for %s in " c_id("%s"),
					type->str().c_str(),
					scope->get_name().c_str());
		}
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref create_bound_id_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_id> &id)
{
	/* this id type does not yet have a bound type. */
	assert(!scope->get_bound_type(id->get_signature()));
	auto env = scope->get_typename_env();
	auto expansion = eval_id(id, env);

	/* however, what it expands to might already have a bound type */
	if (expansion != nullptr) {
		/* we do have an expansion, so let's bind that, then come back and bind
		 * ourselves directly to the llvm version of that */
		auto bound_expansion = upsert_bound_type(status, builder, scope, expansion);

		if (!!status) {
			auto bound_type = scope->get_bound_type(id->get_signature());
			if (bound_type == nullptr) {
				/* it looks like our type was not bound via recursion, let's
				 * bind it now */
				bound_type_t::ref bound_type = bound_type_t::create(
						id,
						bound_expansion->get_location(),
						bound_expansion->get_llvm_type());
				auto program_scope = scope->get_program_scope();
				program_scope->put_bound_type(status, bound_type);
				if (!!status) {
					return bound_type;
				}
			} else {
				wat();
				return bound_type;
			}
		}
	} else {
		user_error(status, id->get_location(), "no type definition found for %s",
				id->str().c_str());
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
			scope->get_typename_env());

	if (expansion != nullptr) {
		return bind_type_expansion(status, builder, scope, expansion,
				scope->make_fqn(operator_->repr()));
	} else {
		user_error(status, operator_->get_location(),
				"unable to expand type: %s",
				operator_->str().c_str());
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref create_bound_maybe_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_maybe> &maybe)
{
	auto program_scope = scope->get_program_scope();
	bound_type_t::ref bound_just_type = upsert_bound_type(status, builder, scope, maybe->just);
	if (!!status) {
		auto llvm_type = bound_just_type->get_llvm_type();
		if (!llvm_type->isPointerTy()) {
			auto bound_type = bound_type_t::create(
					maybe,
					bound_just_type->get_location(),
					llvm_type->getPointerTo());
			program_scope->put_bound_type(status, bound_type);
			if (!!status) {
				return bound_type;
			}
		} else {
			user_error(status, maybe->get_location(),
				   	"type %s cannot be a " c_type("maybe") " type because the underlying storage is already a pointer (it is %s)",
					maybe->str().c_str(),
					llvm_print_type(*llvm_type).c_str());
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
	program_scope->put_bound_type(status, bound_type);
	if (!!status) {
		return bound_type;
	} else {
		return nullptr;
	}
}

bound_type_t::ref create_bound_function_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_function> &function)
{
	bound_type_t::refs args = upsert_bound_types(status,
			builder, scope, function->args->args);

	if (!!status) {
		bound_type_t::ref return_type = upsert_bound_type(
				status, builder, scope, function->return_type);

		if (!!status) {
			auto signature = function->get_signature();
			auto bound_type = scope->get_bound_type(signature);
			if (bound_type) {
				return bound_type;
			} else {
				auto *llvm_fn_type = llvm_create_function_type(status,
						builder, args, return_type);
				if (!!status) {
					bound_type = bound_type_t::create(function,
							function->get_location(), llvm_fn_type);
					ptr<program_scope_t> program_scope = scope->get_program_scope();
					program_scope->put_bound_type(status, bound_type);

					if (!!status) {
						return bound_type;
					} else {
						return nullptr;
					}
				}
			}
		}
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref create_bound_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type::ref type)
{
	assert(!!status);

	indent_logger indent(3,
		string_format("attempting to create a bound type for %s in scope " c_id("%s"),
			type->str().c_str(), scope->get_name().c_str()));

    auto program_scope = scope->get_program_scope();

	if (auto id = dyncast<const types::type_id>(type)) {
		return create_bound_id_type(status, builder, scope, id);
    } else if (auto maybe = dyncast<const types::type_maybe>(type)) {
		return create_bound_maybe_type(status, builder, scope, maybe);
	} else if (auto ref = dyncast<const types::type_ref>(type)) {
		return create_bound_ref_type(status, builder, scope, ref);
	} else if (auto struct_type = dyncast<const types::type_struct>(type)) {
		return create_bound_struct_type(status, builder, scope, struct_type);
	} else if (auto function = dyncast<const types::type_function>(type)) {
		return create_bound_function_type(status, builder, scope, function);
	} else if (auto sum = dyncast<const types::type_sum>(type)) {
		return create_bound_sum_type(status, builder, scope, sum);
	} else if (auto operator_ = dyncast<const types::type_operator>(type)) {
		return create_bound_operator_type(status, builder, scope, operator_);
	} else if (auto variable = dyncast<const types::type_variable>(type)) {
		user_error(status, variable->get_location(), "unable to resolve type for %s", variable->str().c_str());
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
	type = type->rebind(scope->get_type_variable_bindings());

	auto signature = type->get_signature();
	auto bound_type = scope->get_bound_type(signature);
	if (bound_type != nullptr) {
		return bound_type;
	} else {
		/* we believe that this type does not exist. let's build it */
		bound_type = create_bound_type(status, builder, scope, type);

		if (!!status) {
			return bound_type;
		}

		user_error(status, type->get_location(),
			   	"unable to find a definition for %s in scope " c_id("%s"),
				type->str().c_str(),
                scope->get_name().c_str());
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
	if (auto type_function = dyncast<const types::type_function>(function_type->get_type())) {
		auto return_type_sig = type_function->return_type->get_signature();
		auto return_type = scope->get_bound_type(return_type_sig);

		/* this should exist, otherwise how was the function type built in the
		 * first place? */
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
		bool managed,
		const ast::item::ref &node)
{
	atom name = id->get_name();

	/* get the type of this tuple type */
	types::type_struct::ref type = type_struct(get_types(args), {} /* name_index */, managed);
	auto data_type = scope->get_bound_type(type->get_signature());

	if (data_type != nullptr) {
		return data_type;
	} else {
		auto program_scope = scope->get_program_scope();

		/* build the llvm specific type */
		llvm::Type *llvm_tuple_type = llvm_create_struct_type(
				builder, name, args);

		assert_implies(!managed, !"need to treat native types");

		llvm::Type *llvm_wrapped_tuple_type = llvm_wrap_type(builder, program_scope,
				name, llvm_tuple_type);

		/* display the new type */
		llvm::Type *llvm_obj_struct_type = llvm::cast<llvm::PointerType>(llvm_wrapped_tuple_type)->getElementType();
		debug_above(5, log(log_info, "created LLVM wrapped tuple type %s", llvm_print_type(*llvm_obj_struct_type).c_str()));

		/* get the bound type of the data ctor's value */
		bound_type_t::ref data_type = bound_type_t::create(
				type,
				node->token.location,
				managed
					? scope->get_program_scope()->get_bound_type({"__var"})->get_llvm_type()
				   	: llvm_wrapped_tuple_type,
				llvm_wrapped_tuple_type);

		/* put the type for the data type */
		program_scope->put_bound_type(status, data_type);

		if (!!status) {
			return data_type;
		} else {
			return nullptr;
		}
	}
}

std::pair<bound_var_t::ref, bound_type_t::ref> instantiate_tuple_ctor(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		types::type::ref type_fn_context,
		bound_type_t::refs args,
		bool managed,
		identifier::ref id,
		const ast::item::ref &node)
{
#ifdef VAR_T
	/* this is a tuple constructor function */
	if (!!status) {
		program_scope_t::ref program_scope = scope->get_program_scope();

		bound_type_t::ref data_type = get_or_create_tuple_type(status, builder, scope,
				id, args, managed, node);

		if (!!status) {
			bound_var_t::ref tuple_ctor = get_or_create_tuple_ctor(status, builder,
					scope, type_fn_context, data_type, id, node);

			if (!!status) {
				return {tuple_ctor, data_type};
			}
		}
	}
#endif

	assert(!status);
	return {nullptr, nullptr};
}

std::pair<bound_var_t::ref, bound_type_t::ref> instantiate_tagged_tuple_ctor(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		types::type::ref type_fn_context,
		identifier::ref id,
		const ast::item::ref &node,
		types::type::ref type)
{
	assert(id != nullptr);
	assert(type != nullptr);

	/* this is a tuple constructor function */
	if (!!status) {
		program_scope_t::ref program_scope = scope->get_program_scope();
		bound_type_t::ref data_type = upsert_bound_type(status, builder, scope, type);

		if (!!status) {
			debug_above(4, log(log_info, "found bound type %s", data_type->str().c_str()));
			bound_var_t::ref tagged_tuple_ctor = get_or_create_tuple_ctor(status, builder,
					scope, type_fn_context, data_type, id, node);

			if (!!status) {
				return {tagged_tuple_ctor, data_type};
			}
		}
	}

	assert(!status);
	return {nullptr, nullptr};
}

llvm::Value *llvm_call_allocator(
		status_t &status,
		llvm::IRBuilder<> &builder,
	   	program_scope_t::ref program_scope,
	   	const ast::item::ref &node,
		bound_type_t::ref data_type,
		types::type_struct::ref struct_type,
		atom name)
{
	debug_above(5, log(log_info, "calling allocator for %s",
				data_type->str().c_str()));
	bound_var_t::ref mem_alloc_var = program_scope->get_bound_variable(status, node,
			struct_type->managed ? "__create_var" : "__mem_alloc");

	if (!!status) {
		assert(mem_alloc_var != nullptr);

		llvm::Value *llvm_sizeof_tuple = llvm_sizeof_type(builder,
				llvm_deref_type(data_type->get_llvm_specific_type()));

		auto signature = data_type->get_signature();
		debug_above(5, log(log_info, "mapping type " c_type("%s") " to typeid %d",
					signature.str().c_str(), signature.repr().iatom));

		llvm::Value *llvm_alloced = (
				struct_type->managed
				? llvm_create_call_inst(
					status, builder, *node,
					mem_alloc_var,
					{
					/* name this variable */
					builder.CreateGlobalStringPtr(name.str()),

					/* no mark function yet */
					llvm::Constant::getNullValue(
							program_scope->get_bound_type({"__mark_fn"})->get_llvm_type()),

					/* the type_id */
					builder.getInt32(signature.repr().iatom),

					/* allocation size */
					llvm_sizeof_tuple
					})

				: llvm_create_call_inst(status, builder, *node,
					mem_alloc_var, {llvm_sizeof_tuple}));

		return llvm_alloced;
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref get_or_create_tuple_ctor(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		types::type::ref type_fn_context,
		bound_type_t::ref data_type,
		identifier::ref id,
		const ast::item::ref &node)
{
	atom name = id->get_name();

	auto program_scope = scope->get_program_scope();

	types::type::ref type = data_type->get_type();

	debug_above(4, log(log_info, "get_or_create_tuple_ctor evaluating %s with llvm type %s",
				type->str().c_str(),
				llvm_print_type(*data_type->get_llvm_type()).c_str()));
	types::type::ref ctor_args_type;

	if (auto id = dyncast<const types::type_id>(type)) {
		ctor_args_type = eval(type, scope->get_typename_env());
	} else if (auto ref_type = dyncast<const types::type_ref>(type)) {
		ctor_args_type = ref_type;
	} else {
		user_error(status, id->get_location(), "creating a tuple with %s is not yet implemented",
				type->str().c_str());
		return nullptr;
	}

	/* destructure the ref ptr that this should be */
	if (auto ref = dyncast<const types::type_ref>(ctor_args_type)) {
		ctor_args_type = ref->element_type;
	} else {
		user_error(status, id->get_location(), "we should have created a ref type as the return value: %s",
				ctor_args_type->str().c_str());
		return null_impl();
	}

	/* at this point we should have a struct type in ctor_args_type */

	if (ctor_args_type != nullptr) {
		debug_above(4, log(log_info, "get_or_create_tuple_ctor instantiating with type %s -> %s",
					ctor_args_type->str().c_str(), type->str().c_str()));

		auto struct_type = dyncast<const types::type_struct>(ctor_args_type);
		assert(struct_type != nullptr);

		bound_type_t::refs args = upsert_bound_types(status,
				builder, scope, struct_type->dimensions);

		if (!!status) {
			/* save and later restore the current branch insertion point */
			llvm::IRBuilderBase::InsertPointGuard ipg(builder);
			auto function = llvm_start_function(status, builder, scope, node,
					type_fn_context, args, data_type, name);

			if (!!status) {
				llvm::Value *llvm_alloced = llvm_call_allocator(
						status, builder, program_scope, node, data_type,
						struct_type, name);

				if (!!status) {
					assert(data_type->get_llvm_type() != nullptr);
					if (data_type->get_llvm_type()->isPointerTy()) {
						/* we've allocated enough space for the object type,
						 * let's get our allocation as such */
						llvm::Value *llvm_final_obj = builder.CreatePointerBitCastOrAddrSpaceCast(
								llvm_alloced, 
								data_type->get_llvm_specific_type());

						int index = 0;

						llvm::Function *llvm_function = (llvm::Function *)function->llvm_value;
						llvm::Function::arg_iterator args_iter = llvm_function->arg_begin();
						while (args_iter != llvm_function->arg_end()) {
							llvm::Value *llvm_param = args_iter++;
							/* get the location we should store this datapoint in */
							llvm::Value *llvm_gep = llvm_make_gep(builder, llvm_final_obj,
									index++, struct_type->managed);
							debug_above(5, log(log_info, "store %s at %s", llvm_print_value(*llvm_param).c_str(),
										llvm_print_value(*llvm_gep).c_str()));
							builder.CreateStore(llvm_param, llvm_gep);
						}

						/* create a return statement for the final object. */
						builder.CreateRet(llvm_final_obj);

						llvm_verify_function(status, llvm_function);

						if (!!status) {
							/* bind the ctor to the program scope */
							scope->get_program_scope()->put_bound_variable(status, name, function);

							if (!!status) {
								debug_above(10, log(log_info, "module so far is:\n" c_ir("%s"), llvm_print_module(
												*llvm_get_module(builder)).c_str()));
								return function;
							}
						}
					} else {
						user_error(status, node->get_location(),
								"data type %s is not a pointer type",
								data_type->str().c_str());
					}
				}
			}
		}
	} else {
		user_error(status, node->get_location(),
				"could not figure out what to do with %s",
				type->str().c_str());
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


llvm::Value *llvm_make_gep(
		llvm::IRBuilder<> &builder,
	   	llvm::Value *llvm_value,
	   	int index,
	   	bool managed)
{
	debug_above(5, log(log_info,
			   	"creating GEP+load for %s%s[%d]",
			   	managed ? "managed " : "",
			   	llvm_print_value(*llvm_value).c_str(), index));

	std::vector<llvm::Value *> gep_path = (
			managed

			/* the physical layout of managed types wraps the logical
			 * type, so, if this is a managed type, let's unbox the
			 * managed wrapper to get to the inner cell data */
			? std::vector<llvm::Value *>{
			builder.getInt32(0),
			builder.getInt32(1),
			builder.getInt32(index)}

			/* native types can be accessed directly */
			: std::vector<llvm::Value *>{
			builder.getInt32(0),
			builder.getInt32(index)});

	return builder.CreateInBoundsGEP(llvm_value, gep_path);
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
		if (auto struct_type = dyncast<const types::type_struct>(lhs->type->get_type())) {
			if (struct_type->dimensions.size() > subscript_index) {
				/* ok, we're in range */
				debug_above(6, log(log_info, "generating dereference %s[%d]",
							lhs->str().c_str(), subscript_index));

				bound_type_t::ref data_type = upsert_bound_type(status, builder,
						scope, struct_type->dimensions[subscript_index]);

				if (!!status) {
					/* get the tuple */
					llvm::Value *llvm_lhs = llvm_resolve_alloca(builder, lhs->llvm_value);
					if (lhs->type->get_llvm_specific_type()->isPointerTy()) {
						llvm::Value *llvm_lhs_subtype = builder.CreatePointerBitCastOrAddrSpaceCast(
								llvm_lhs,
								lhs->type->get_llvm_specific_type());

						llvm::Value *llvm_value = builder.CreateLoad(llvm_make_gep(builder,
									llvm_lhs_subtype, subscript_index,
									struct_type->managed));

						return bound_var_t::create(
								INTERNAL_LOC(),
								"temp_deref_subscript",
								data_type,
								llvm_value,
								make_code_id(node->token),
								false/*is_lhs*/);
					} else {
						user_error(status, node->get_location(),
								"lhs type %s is not a pointer type",
								struct_type->str().c_str());
					}
				}
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
