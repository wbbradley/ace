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
				bound_type->get_llvm_specific_type()->getPointerTo());
	}
				
	/* we've never seen the internal type, so start by registering an
	 * opaque placeholder for the struct's concrete type */
	
	/* first create the opaque pointer type */
	auto bound_pointer_type = create_ref_ptr_type(builder, ref_type);
	program_scope->put_bound_type(status, bound_pointer_type);

	debug_above(6, log("create_bound_ref_type(..., %s)",
				ref_type->str().c_str()));
	
	/* before we return the pointer type, let's go ahead and instantiate
	 * the actual structural type */
	auto element = upsert_bound_type(status, builder, scope, ref_type->element_type);

	if (!!status) {
		// auto llvm_element_type = llvm::dyn_cast<llvm::StructType>(element->get_llvm_specific_type());
		// assert(llvm_element_type != nullptr);
		// assert(!llvm_element_type->isOpaque());

		// auto bound_element_type = scope->get_bound_type(ref_type->element_type->get_signature());
		// assert(bound_element_type != nullptr);
		// assert(bound_element_type->get_llvm_specific_type() == element->get_llvm_specific_type());

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
			llvm::StructType *inner_struct = llvm_create_struct_type(
					builder, struct_type->get_signature(), bound_dimensions);

			inner_struct->setName(type_struct(
						struct_type->dimensions,
						struct_type->name_index,
						false /* managed */)->get_signature().str());

			/* now place the logical data into the structure */
			elements.push_back(inner_struct);
		} else {
			/* this is a native structure, let's just iterate over the bound
			 * dimensions and place those directly into this structure */
			for (auto bound_dimension : bound_dimensions) {
				elements.push_back(bound_dimension->get_llvm_specific_type());
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
	}

	/* tuples don't have names, so there's no need for a placeholder, as
	 * they cannot be self referential */

	/* make sure one of these doesn't already exist */
	assert(!scope->get_bound_type(struct_type->get_signature()));

	/* get the pointer type to this, if it exists, get the opaque struct
	 * pointer that it had created. fill it out. if it doesn't exist,
	 * create it, then extract this tuple type from that. */
	types::type::ref ref_type = type_ref(struct_type);
	bound_type_t::ref bound_ref_type = upsert_bound_type(status, builder, scope, ref_type);

	if (auto bound_type = scope->get_bound_type(struct_type->get_signature())) {
		/* while instantiating our pointer type, we also instantiated this */
		return bound_type;
	}

	if (bound_ref_type != nullptr) {
		/* fetch the previously created pointer to this type */
		llvm::StructType *llvm_struct_type = llvm::dyn_cast<llvm::StructType>(llvm::cast<llvm::PointerType>(
					bound_ref_type->get_llvm_specific_type())->getElementType());
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
						bound_expansion->get_llvm_type(),
						bound_expansion->get_llvm_specific_type());
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

	/* apply the operator */
	auto expansion = eval_apply(operator_->oper, operator_->operand,
			scope->get_typename_env());

	if (expansion != nullptr) {
		/* make sure this isn't already bound */
		auto signature = operator_->get_signature();
		assert(scope->get_bound_type(signature) == nullptr);

		auto program_scope = scope->get_program_scope();

		/* set up a mapping at the program level for these bound types */
		program_scope->put_bound_type_mapping(status, signature,
				expansion->get_signature());

		/* make sure we have a bound type for the expansion type. NB: this will
		 * rely on the bound type mapping to ensure that when the instantiation
		 * process looks for our operator_ type (via self-referencing type
		 * definitions) it translates into whatever cycle breaking we already
		 * have. */
		auto expansion_bound_type = upsert_bound_type(status, builder, scope,
				expansion);

		if (!!status) {
			/* let's finally create the official bound type for this operator */
			auto bound_type = bound_type_t::create(
					operator_,
					operator_->get_location(),
					expansion_bound_type->get_llvm_type(),
					expansion_bound_type->get_llvm_specific_type());

			program_scope->put_bound_type(status, bound_type);
			if (!!status) {
				return bound_type;
			}
		}
	} else {
		user_error(status, operator_->get_location(),
				"unable to expand type operation %s",
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
		auto llvm_type = bound_just_type->get_llvm_specific_type();
		if (llvm_type->isPointerTy()) {
			debug_above(5, log(log_info,
						"creating maybe type for %s",
						maybe->just->str().c_str()));
			auto bound_type = scope->get_bound_type(maybe->get_signature());
			if (bound_type == nullptr) {
				bound_type = bound_type_t::create(
						maybe,
						bound_just_type->get_location(),
						llvm_type);
				program_scope->put_bound_type(status, bound_type);
			}

			if (!!status) {
				return bound_type;
			}
		} else {
			user_error(status, maybe->get_location(),
				   	"type %s cannot be a " c_type("maybe") " type because the underlying storage is not a pointer (it is %s)",
					maybe->str().c_str(),
					llvm_print_type(llvm_type).c_str());
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
		/* this case is critical for breaking cycles during structure
		 * instantiation */
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
	/* this is a tuple constructor function */
	if (!!status) {
		program_scope_t::ref program_scope = scope->get_program_scope();

		types::type_ref::ref type = type_ref(type_struct(get_types(args), {} /* name_index */, managed));
		bound_type_t::ref data_type = upsert_bound_type(status, builder, scope, type);

		if (!!status) {
			bound_var_t::ref tuple_ctor = get_or_create_tuple_ctor(status, builder,
					scope, type_fn_context, data_type, id, node);

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

llvm::Constant *llvm_dim_offset_gep(llvm::StructType *llvm_struct_type, int index) {
	auto &llvm_context = llvm_struct_type->getContext();
	llvm::Constant *llvm_gep_index[] = {
		llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_context), 0),
		llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_context), 1),
		llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_context), index),
	};
	llvm::Constant *llvm_gep = llvm::ConstantExpr::getGetElementPtr(
			llvm_struct_type,
			llvm::Constant::getNullValue(llvm::PointerType::getUnqual(llvm_struct_type)), llvm_gep_index);

	return llvm::ConstantExpr::getPtrToInt(llvm_gep, llvm::Type::getInt64Ty(llvm_struct_type->getContext()));
}

llvm::Value *llvm_call_allocator(
		status_t &status,
		llvm::IRBuilder<> &builder,
	   	program_scope_t::ref program_scope,
		life_t::ref life,
	   	const ast::item::ref &node,
		bound_type_t::ref data_type,
		types::type_struct::ref struct_type,
		atom name,
		bound_type_t::refs args)
{
	debug_above(5, log(log_info, "calling allocator for %s",
				data_type->str().c_str()));
	bound_var_t::ref mem_alloc_var = program_scope->get_bound_variable(status, node,
			struct_type->managed ? "__create_var" : "__mem_alloc");

	if (!!status) {
		assert(mem_alloc_var != nullptr);

		llvm::Constant *llvm_type_info = nullptr;
		llvm::Value *llvm_sizeof_tuple = llvm_sizeof_type(builder,
				llvm_deref_type(data_type->get_llvm_specific_type()));

		if (struct_type->managed) {
			llvm::StructType *llvm_struct_type = llvm::dyn_cast<llvm::StructType>(
					llvm::dyn_cast<llvm::PointerType>(
						data_type->get_llvm_specific_type())->getElementType());
			assert(llvm_struct_type != nullptr);
			assert(llvm_struct_type->elements().size() != 0);

			/* calculate the type map */
			std::vector<llvm::Constant *> llvm_offsets;
			for (size_t i=0; i<args.size(); ++i) {
				if (args[i]->is_managed()) {
					/* this element is managed, so let's store its memory offset in
					 * our array */
					llvm_offsets.push_back(llvm::ConstantExpr::getTrunc(
								llvm_dim_offset_gep(llvm_struct_type, i),
								builder.getInt16Ty(), true));
				}
			}

			/* now let's create a placeholder type for the dim offsets map */
			llvm::ArrayType *llvm_dim_offsets_type = llvm::ArrayType::get(
					builder.getInt16Ty(), llvm_offsets.size());

			llvm::Module *llvm_module = llvm_get_module(builder);

			/* create the actual list of offsets */
			llvm::Constant *llvm_dim_offsets = llvm_get_global(llvm_module,
					std::string("__dim_offsets_") + name.str(),
					llvm::ConstantExpr::getPointerBitCastOrAddrSpaceCast(
						llvm::ConstantArray::get(llvm_dim_offsets_type,
							llvm_offsets),
						builder.getInt16Ty()->getPointerTo()));

					llvm::StructType *llvm_type_info_type = llvm::cast<llvm::StructType>(
						program_scope->get_bound_type({"__type_info"})->get_llvm_type());

					auto signature = data_type->get_signature();
					debug_above(5, log(log_info, "mapping type " c_type("%s") " to typeid %d",
							signature.str().c_str(), signature.repr().iatom));

					llvm_type_info = llvm_get_global(llvm_module, string_format("__type_info_%s", signature.str().c_str()),
						llvm::ConstantStruct::get(
							llvm_type_info_type,

							/* the type_id */
							builder.getInt32(signature.repr().iatom),

							/* the number of contained references */
							builder.getInt16(llvm_offsets.size()),

							/* the actual offsets to the managed references */
							llvm_dim_offsets,

							/* name this variable */
							builder.CreateGlobalStringPtr(name.str()),

							/* allocation size */
							llvm_sizeof_tuple,

							nullptr));
		}

		llvm::Value *llvm_alloced = (
				struct_type->managed
				? llvm_create_call_inst(
					status, builder, *node,
					mem_alloc_var,
					{
					/* the type info for this value */
					llvm_type_info,
					}, life)

				: llvm_create_call_inst(status, builder, *node,
					mem_alloc_var, {llvm_sizeof_tuple}, life));

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
				llvm_print_type(data_type->get_llvm_specific_type()).c_str()));
	types::type::ref expanded_type;

	expanded_type = eval(type, scope->get_typename_env());
	if (expanded_type == nullptr) {
		expanded_type = type;
	}

	/* destructure the ref ptr that this should be */
	if (auto ref = dyncast<const types::type_ref>(expanded_type)) {
		expanded_type = ref->element_type;
	} else {
		user_error(status, id->get_location(), "we should have created a ref type as the return value: %s",
				expanded_type->str().c_str());
		return null_impl();
	}

	/* at this point we should have a struct type in expanded_type */
	if (auto struct_type = dyncast<const types::type_struct>(expanded_type)) {
		bound_type_t::refs args = upsert_bound_types(status,
				builder, scope, struct_type->dimensions);

		if (!!status) {
			/* save and later restore the current branch insertion point */
			llvm::IRBuilderBase::InsertPointGuard ipg(builder);

			auto function = llvm_start_function(status, builder, scope, node,
					type_fn_context, args, data_type, name);

			life_t::ref life = make_ptr<life_t>(lf_function);

			if (!!status) {
				llvm::Value *llvm_alloced = llvm_call_allocator(
						status, builder, program_scope, life, node, data_type,
						struct_type, name, args);

				if (!!status) {
					assert(data_type->get_llvm_type() != nullptr);
			
					/* we've allocated enough space for the object type,
					 * let's get our allocation as such */
					llvm::Value *llvm_final_obj = llvm_maybe_pointer_cast(builder,
							llvm_alloced, data_type);

					int index = 0;

					llvm::Function *llvm_function = (llvm::Function *)function->llvm_value;
					llvm::Function::arg_iterator args_iter = llvm_function->arg_begin();
					while (args_iter != llvm_function->arg_end()) {
						llvm::Value *llvm_param = &*args_iter++;
						/* get the location we should store this datapoint in */
						llvm::Value *llvm_gep = llvm_make_gep(builder, llvm_final_obj,
								index, struct_type->managed);
						llvm_gep->setName(string_format("address_of.member.%d", index));

						debug_above(5, log(log_info, "store %s at %s", llvm_print_value(*llvm_param).c_str(),
									llvm_print_value(*llvm_gep).c_str()));
						builder.CreateStore(llvm_param, llvm_gep);

						if (args[index]->is_managed()) {
							debug_above(5, log(log_info, "inserting call to addref_var for member %d",
										index));

							/* addref the contained variables in this structure
							 * because we have taken new references to them */
							auto release_function = program_scope->get_singleton({"__construct_var"});
							call_program_function(
									status,
									builder,
									scope,
									life,
									{"__addref_var"},
									node,
									{bound_var_t::create(
											INTERNAL_LOC(),
											"temp_ctor_dim",
											args[index],
											llvm_param,
											make_iid("ctor_dim_value"),
											false /*is_lhs*/)});

							if (!status) {
								break;
							}
						}

						++index;
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
		life_t::ref life,
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
		return call_program_function(status, builder, scope, life, "__getitem__",
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
		life_t::ref life,
		const ast::item::ref &node,
		bound_var_t::ref lhs,
		identifier::ref index_id,
		uint64_t subscript_index)
{
	debug_above(6, log(log_info, "generating dereference %s[%d]", lhs->str().c_str(), subscript_index));

	/* do some checks on the lhs */
	if (auto struct_type = dyncast<const types::type_struct>(lhs->type->get_type())) {
		if (struct_type->dimensions.size() > subscript_index) {
			/* ok, we're in range */
			debug_above(6, log(log_info, "generating dereference %s[%d]",
						lhs->str().c_str(), (int)subscript_index));

			bound_type_t::ref data_type = upsert_bound_type(status, builder,
					scope, struct_type->dimensions[subscript_index]);

			if (!!status) {
				/* get the tuple */
				llvm::Value *llvm_lhs_subtype = llvm_maybe_pointer_cast(builder,
						lhs->llvm_value, lhs->type);

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
			}
		} else {
			user_error(status, *node, "index out of range");
		}
	} else {
		return type_check_get_item_with_int_literal(status, builder, scope, life,
				node, lhs, index_id, subscript_index);
	}

	assert(!status);
	return nullptr;
}
