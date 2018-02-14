#include "zion.h"
#include "atom.h"
#include "ast.h"
#include "compiler.h"
#include "llvm_utils.h"
#include "llvm_types.h"
#include "code_id.h"
#include "logger.h"
#include <iostream>
#include "unification.h"
#include "type_kind.h"

bound_type_t::refs upsert_bound_types(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type_t::refs types)
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

template <typename T>
bound_type_t::ref create_ptr_type(
		llvm::IRBuilder<> &builder,
		T ptr_type)
{
	debug_above(4, log(log_info, "creating ptr type for %s",
				ptr_type->element_type->str().c_str()));
	llvm::StructType *llvm_type = llvm::StructType::create(
			builder.getContext(),
			ptr_type->element_type->get_signature());
	assert(!llvm_type->isSized());
	assert(llvm_type->isOpaque());

	return bound_type_t::create(
			ptr_type,
			ptr_type->get_location(),
			llvm_type->getPointerTo());
}

bound_type_t::ref create_bound_ref_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type_ref_t::ref type_ref)
{
	/* create a bound_type for a ref type */
	ptr<program_scope_t> program_scope = scope->get_program_scope();

	assert(!scope->get_bound_type(type_ref->get_signature()));

	auto ftvs = type_ref->get_ftvs();
	if (ftvs.size() != 0) {
		user_error(status, type_ref->get_location(),
				"unable to instantiate type %s because free variables [%s] still exist",
				type_ref->str().c_str(),
				join_with(ftvs, ", ", [] (std::string a) -> std::string {
					return string_format(c_id("%s"), a.c_str());
				}).c_str());
	}

	if (!!status) {
		/* get the element type's bound type, if it exists */
		bound_type_t::ref bound_type = scope->get_bound_type(type_ref->element_type->get_signature());

		if (bound_type != nullptr && bound_type->get_llvm_specific_type()->isVoidTy()) {
			user_error(status, type_ref->get_location(),
					"cannot create references to " c_type("void"));
		}
		if (!!status) {
			/* make sure we create the pointed to type first */
			bound_type_t::ref bound_element_type = upsert_bound_type(status, builder, scope, type_ref->element_type);

			if (!!status) {
				auto bound_type = bound_type_t::create(type_ref,
						type_ref->get_location(),
						bound_element_type->get_llvm_specific_type()->getPointerTo());
				program_scope->put_bound_type(status, bound_type);
				return bound_type;
			}
		}
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref create_bound_extern_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type_extern_t::ref type_extern)
{
	/* create a bound_type for a ref type */
	ptr<program_scope_t> program_scope = scope->get_program_scope();

	assert(!scope->get_bound_type(type_extern->get_signature()));

	auto ftvs = type_extern->get_ftvs();
	if (ftvs.size() != 0) {
		user_error(status, type_extern->get_location(),
				"unable to instantiate type %s because free variables [%s] still exist",
				type_extern->str().c_str(),
				join_with(ftvs, ", ", [] (std::string a) -> std::string {
					return string_format(c_id("%s"), a.c_str());
				}).c_str());
	}

	if (!!status) {
		bound_type_t::ref var_ref_type = program_scope->get_runtime_type(status, builder, "var_t", true /*get_ptr*/);
		if (!!status) {
			auto bound_type = bound_type_t::create(type_extern,
					type_extern->get_location(),
					var_ref_type->get_llvm_type());
			program_scope->put_bound_type(status, bound_type);
			return bound_type;
		}
	}

	assert(!status);
	return nullptr;
}

template <typename T>
bound_type_t::ref create_bound_ptr_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const T &type_ptr)
{
	/* create a bound_type for a pointer type */
	ptr<program_scope_t> program_scope = scope->get_program_scope();

	assert(!scope->get_bound_type(type_ptr->get_signature()));

	auto ftvs = type_ptr->get_ftvs();
	if (ftvs.size() != 0) {
		user_error(status, type_ptr->get_location(),
				"unable to instantiate type %s because free variables [%s] still exist",
				type_ptr->str().c_str(),
				join_with(ftvs, ", ", [] (std::string a) -> std::string {
					return string_format(c_id("%s"), a.c_str());
					}).c_str());
		return nullptr;
	}

	debug_above(8, log("recursing in to create element type %s", type_ptr->element_type->str().c_str()));
	/* get or create the element type's bound type, if it exists */
	bound_type_t::ref bound_type = upsert_bound_type(status, builder, scope, type_ptr->element_type);
	debug_above(8, log("recursion yielded bound type %s", bound_type->str().c_str()));

	if (!!status) {
		assert(bound_type != nullptr);
		bound_type_t::ref bound_ptr_type = scope->get_bound_type(type_ptr->get_signature(), false /*use_mapping*/);
		if (bound_ptr_type != nullptr) {
			debug_above(8, log("create_bound_ptr_type(%s) returning bound type %s",
				   type_ptr->str().c_str(),
			   	   bound_ptr_type->str().c_str()));
			assert(type_ptr->repr() == bound_ptr_type->get_type()->repr());
			return bound_ptr_type;
		}

		bound_ptr_type = bound_type_t::create(type_ptr,
				type_ptr->get_location(),
				bound_type->get_llvm_specific_type()->getPointerTo());
		program_scope->put_bound_type(status, bound_ptr_type);

		if (!!status) {
			return bound_ptr_type;
		}
	}

	assert(!status);
	return nullptr;
}

std::vector<llvm::Type *> build_struct_elements(
		status_t &status,
		llvm::IRBuilder<> &builder,
	   	program_scope_t::ref program_scope,
		types::type_struct_t::ref struct_type,
		bound_type_t::refs bound_dimensions,
		bool native)
{
	std::vector<llvm::Type *> elements;

	for (auto &dimension : bound_dimensions) {
		assert(!dimension->get_type()->is_ref());
	}

	if (native) {
		/* just add all the dimensions of the native struct */
		for (auto &dimension : bound_dimensions) {
			elements.push_back(dimension->get_llvm_specific_type());
		}

		return elements;
	} else {
		/* let's prefix the data in this structure with the managed runtime */
		bound_type_t::ref var_type = program_scope->get_runtime_type(status, builder, "var_t");
		if (!!status) {
			/* place the var_t struct into the structure */
			elements.push_back(var_type->get_llvm_type());
			llvm::StructType *inner_struct = llvm_create_struct_type(
					builder, struct_type->get_signature(), bound_dimensions);

			/* make a name for this inner managed struct */
			std::stringstream ss;
			ss << "struct{";
			join_dimensions(ss, 
					struct_type->dimensions,
					struct_type->name_index, {});
			ss << "}";

			inner_struct->setName(ss.str());

			/* now place the logical data into the structure */
			elements.push_back(inner_struct);

			assert(elements.size() == 2);

			return elements;
		}
	}

	assert(!status);
	return {};
}

bound_type_t::ref create_bound_managed_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_managed_t> &managed_type)
{
	ptr<program_scope_t> program_scope = scope->get_program_scope();

	if (managed_type->ftv_count() != 0) {
		debug_above(5, log(log_info,
					"found abstract type %s when attempting to create a bound type",
					managed_type->str().c_str()));
	}

	/* tuples don't have names, so there's no need for a placeholder, as
	 * they cannot be self referential */

	/* make sure one of these doesn't already exist */
	assert(!scope->get_bound_type(managed_type->get_signature()));

	/* get the pointer type to this, if it exists, get the opaque struct
	 * pointer that it had created. fill it out. if it doesn't exist,
	 * create it, then extract this tuple type from that. */
	types::type_ptr_t::ref managed_ptr_type = type_ptr(managed_type);
	bound_type_t::ref bound_ptr_type = scope->get_bound_type(managed_ptr_type->get_signature());

	if (bound_ptr_type == nullptr) {
		bound_ptr_type = create_ptr_type(builder, managed_ptr_type);
		program_scope->put_bound_type(status, bound_ptr_type);
	}

	debug_above(5, log(log_info,
			"checking whether %s is bound",
		managed_type->str().c_str()));	

	assert(!scope->get_bound_type(managed_type->get_signature()));

	if (bound_ptr_type != nullptr) {
		/* fetch the previously created pointer to this type */
		llvm::PointerType *llvm_pointer_type = llvm::cast<llvm::PointerType>(bound_ptr_type->get_llvm_specific_type());
		assert(llvm_pointer_type != nullptr);

		llvm::StructType *llvm_struct_type = llvm::dyn_cast<llvm::StructType>(llvm_pointer_type->getElementType());
		assert(llvm_struct_type != nullptr);

		assert(!llvm_struct_type->isSized());
		assert(llvm_struct_type->isOpaque());

		auto struct_type = dyncast<const types::type_struct_t>(managed_type->element_type);
		assert(struct_type != nullptr);
		auto var_type = program_scope->get_runtime_type(status, builder, "var_t");
		if (!!status) {
			/* ensure that since this type is managed we refer to it generally by
			 * its managed structure definition (upwards pointer bitcasts happen
			 * automatically at reference locations) */
			llvm::Type *llvm_least_specific_type = var_type->get_llvm_type();

			/* resolve all of the contained dimensions. NB: cycles should be broken
			 * by the existence of the pointer to this type */
			bound_type_t::refs bound_dimensions = upsert_bound_types(status,
					builder, scope, types::without_refs(struct_type->dimensions));

			if (!!status) {
				/* fill out the internals of this structure INCLUDING the MANAGED var_t */
				std::vector<llvm::Type *> elements = build_struct_elements(
						status, builder, program_scope, struct_type, bound_dimensions, false /*native*/);
				if (!!status) {
					/* finally set the elements into the structure */
					llvm_struct_type->setBody(elements);
					debug_above(6, log("setting the body of the managed %s structure to %s",
								var_type->get_signature().str().c_str(),
								llvm_print(llvm_struct_type).c_str()));

					auto bound_type = bound_type_t::create(managed_type,
							struct_type->get_location(), llvm_least_specific_type,
							llvm_struct_type);

					/* register this type */
					program_scope->put_bound_type(status, bound_type);

					if (!!status) {
						return bound_type;
					}
				}
			}
		}
	} else {
		user_error(status, managed_type->get_location(),
				"cyclical type definition? %s",
				managed_type->str().c_str());
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref create_bound_tuple_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_tuple_t> &tuple_type)
{
	auto program_scope = scope->get_program_scope();
	auto expansion = type_ptr(type_managed(type_struct(tuple_type->dimensions, {})));

	auto bound_structural_type = upsert_bound_type(status, builder, scope, expansion);
	if (!!status) {
		auto bound_type = bound_type_t::create(tuple_type,
				tuple_type->get_location(), bound_structural_type->get_llvm_type(),
				bound_structural_type->get_llvm_specific_type());
		program_scope->put_bound_type(status, bound_type);
		if (!!status) {
			return bound_type;
		}
	}
	
	assert(!status);
	return nullptr;
}

bound_type_t::ref create_bound_struct_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_struct_t> &struct_type)
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
	types::type_ptr_t::ref ptr_type = type_ptr(struct_type);
	bound_type_t::ref bound_ptr_type = scope->get_bound_type(ptr_type->get_signature());

	if (bound_ptr_type == nullptr) {
		bound_ptr_type = create_ptr_type(builder, ptr_type);
		program_scope->put_bound_type(status, bound_ptr_type);
	}

	if (!!status) {
		assert(bound_ptr_type != nullptr);
		llvm::PointerType *llvm_ptr_type = llvm::dyn_cast<llvm::PointerType>(bound_ptr_type->get_llvm_specific_type());
		assert(llvm_ptr_type != nullptr);

		/* fetch the previously created pointer to this type */
		llvm::StructType *llvm_struct_type = llvm::dyn_cast<llvm::StructType>(llvm_ptr_type->getElementType());
		assert(llvm_struct_type != nullptr);

		assert(!llvm_struct_type->isSized());
		assert(llvm_struct_type->isOpaque());

		debug_above(6, log("found pointer type %s with opaque element type, now we will instantiate the concrete struct",
					bound_ptr_type->str().c_str()));

		/* resolve all of the contained dimensions. NB: cycles should be broken
		 * by the existence of the pointer to this type */
		bound_type_t::refs bound_dimensions = upsert_bound_types(status,
				builder, scope, types::without_refs(struct_type->dimensions));

		if (!!status) {
			/* fill out the internals of this structure */
			std::vector<llvm::Type *> elements = build_struct_elements(
					status, builder, program_scope, struct_type, bound_dimensions, true /*native*/);
			if (!!status) {
				/* finally set the elements into the structure */
				assert(llvm_struct_type->isOpaque());

				llvm_struct_type->setBody(elements);
				debug_above(6, log("setting the body of the %s structure to %s",
							struct_type->get_signature().c_str(),
							llvm_print(llvm_struct_type).c_str()));

				auto bound_type = bound_type_t::create(struct_type,
						struct_type->get_location(), llvm_struct_type,
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
		}
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref bind_expansion(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
	   	types::type_t::ref unexpanded,
	   	types::type_t::ref expansion)
{
	debug_above(7, log("binding expansion with unexpanded: %s / expansion: %s",
				unexpanded->str().c_str(),
				expansion->str().c_str()));
	/* make sure this isn't already bound */
	auto unexpanded_signature = unexpanded->get_signature();
	auto expanded_signature = expansion->get_signature();
	assert(scope->get_bound_type(unexpanded_signature) == nullptr);

	auto program_scope = scope->get_program_scope();

	/* set up a mapping at the program level for these bound types */
	if (unexpanded_signature != expanded_signature) {
		program_scope->put_bound_type_mapping(status, unexpanded_signature,
				expansion->get_signature());
		program_scope->put_bound_type_mapping(status, type_ptr(unexpanded)->get_signature(),
				type_ptr(expansion)->get_signature());
	}

	/* make sure we have a bound type for the expansion type. NB: this will
	 * rely on the bound type mapping to ensure that when the instantiation
	 * process looks for our unexpanded type (via self-referencing type
	 * definitions) it translates into whatever cycle breaking we already
	 * have. */
	auto expansion_bound_type = upsert_bound_type(status, builder, scope, expansion);

	if (!!status) {
		llvm::StructType *llvm_struct_type = llvm_find_struct(
				expansion_bound_type->get_llvm_specific_type());
		if (llvm_struct_type != nullptr) {
			std::string old_name = llvm_struct_type->getName().str();
			if (old_name.substr(0, 2) != "z.") {
				std::string new_name = std::string("z.") + unexpanded->repr(scope->get_type_variable_bindings());
				debug_above(6, log("changing name of LLVM struct from %s to %s",
							old_name.c_str(),
							new_name.c_str()));
				llvm_struct_type->setName(new_name);
			}
		}

		bound_type_t::ref bound_type = program_scope->get_bound_type(unexpanded_signature, false /*use_mappings*/);

		if (bound_type == nullptr) {
			/* let's finally create the official bound type for this operator, assuming it didn't
			 * happen already. */
			bound_type = bound_type_t::create(
					unexpanded,
					unexpanded->get_location(),
					expansion_bound_type->get_llvm_type(),
					expansion_bound_type->get_llvm_specific_type());

			program_scope->put_bound_type(status, bound_type);
		}

		if (!!status) {
			upsert_bound_type(status, builder, scope, type_ptr(expansion));
			if (!!status) {
				return bound_type;
			}
		}
	}
	assert(!status);
	return nullptr;
}

bound_type_t::ref create_bound_expr_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_t> &id)
{
	/* this id type does not yet have a bound type. */
	assert(!scope->get_bound_type(id->get_signature()));
	auto nominal_expansion = id->eval(scope, false);
	if (auto bound_type = scope->get_bound_type(nominal_expansion->get_signature(), false)) {
		if (id->get_signature() != nominal_expansion->get_signature()) {
			/* make it so we can quickly find this type even though the fully normalized nominal
			 * name is different. */
			scope->get_program_scope()->put_bound_type_mapping(status, id->get_signature(), nominal_expansion->get_signature());
		}

		if (!!status) {
			/* once nominally expanded, this id is already bound. use the nominal expansion as the
			 * source of truth */
			return bound_type;
		}
	}

	if (!!status) {
		auto total_expansion = id->eval(scope, true);

		debug_above(6, log("create_bound_type(..., %s) [nominal = %s, total = %s] in env %s",
					id->str().c_str(),
					nominal_expansion->str().c_str(),
					total_expansion->str().c_str(),
					str(scope->get_nominal_env()).c_str()));

		/* however, what it expands to might already have a bound type */
		if (total_expansion != id) {
			return bind_expansion(status, builder, scope, nominal_expansion, total_expansion);
		} else {
			user_error(status, id->get_location(), "no type definition found for %s in [%s]",
					id->str().c_str(),
					join(keys(scope->get_total_env()), ", ").c_str());
		}
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref create_bound_integer_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_integer_t> &integer)
{
	auto nominal_env = scope->get_nominal_env();
	auto signed_ = integer->signed_->eval(scope, true);

	types::type_t::ref bit_size_expansion;
	int bit_size = types::coerce_to_integer(status, nominal_env, scope->get_total_env(), integer->bit_size, bit_size_expansion);
	if (!!status) {
		if (!types::is_type_id(signed_, "true") && !types::is_type_id(signed_, "false")) {
			user_error(status, integer->get_location(), "could not determine signedness for type from %s",
					signed_->str().c_str());
		} else {
			if (bit_size != 1 &&
					bit_size != 8 &&
					bit_size != 16 &&
					bit_size != 32 &&
					bit_size != 64 &&
					bit_size != 128)
			{
				user_error(status, integer->get_location(), "illegal bit-size for %s",
						integer->str().c_str());
			} else {
				auto bound_type = bound_type_t::create(
						type_integer(bit_size_expansion, signed_),
						integer->get_location(),
						builder.getIntNTy(bit_size),
						builder.getIntNTy(bit_size));
				scope->get_program_scope()->put_bound_type(status, bound_type);
				return bound_type;
			}
		}
	}
	assert(!status);
	return nullptr;
}

bound_type_t::ref create_bound_maybe_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_maybe_t> &maybe)
{
	auto program_scope = scope->get_program_scope();

	assert(program_scope->get_bound_type(maybe->get_signature(), false /*use_mappings*/) == nullptr);

	if (auto bound_just_type = program_scope->get_bound_type(
				maybe->just->get_signature(), false /*use_mappings*/))
   	{
		if (bound_just_type->get_llvm_type()->isPointerTy()) {
			/* try breaking a cycle of maybes */
			auto bound_type = bound_type_t::create(
					maybe,
					bound_just_type->get_location(),
					bound_just_type->get_llvm_type(),
					bound_just_type->get_llvm_specific_type());
			program_scope->put_bound_type(status, bound_type);
			return bound_type;
		} else {
			user_error(status, bound_just_type->get_location(),
					"maybe types must wrap pointers. %s is not a pointer",
					maybe->just->str().c_str());
		}
	}

	if (!!status) {
		program_scope->put_bound_type_mapping(status, maybe->get_signature(),
				maybe->just->get_signature());

		if (!!status) {
			bound_type_t::ref bound_just_type = upsert_bound_type(status, builder, scope, maybe->just);
			if (!!status) {
				auto llvm_type = bound_just_type->get_llvm_specific_type();
				if (llvm_type->isPointerTy()) {
					debug_above(5, log(log_info,
								"creating maybe type for %s",
								maybe->just->str().c_str()));
					auto bound_type = scope->get_bound_type(maybe->get_signature(), false /*use_mappings*/);
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
							llvm_print(llvm_type).c_str());
				}
			}
		}
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref create_bound_sum_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_sum_t> &sum)
{
	assert(!scope->get_bound_type(sum->get_signature()));
	auto var_ptr_type = scope->get_program_scope()->get_runtime_type(status, builder, "var_t", true /*get_ptr*/);
	if (!!status) {
		auto bound_type = bound_type_t::create(sum,
				sum->get_location(),
				var_ptr_type->get_llvm_type());

		ptr<program_scope_t> program_scope = scope->get_program_scope();
		program_scope->put_bound_type(status, bound_type);

		/* check for disallowed types */
		for (auto subtype : sum->options) {
			if (!types::is_managed_ptr(subtype, scope->get_nominal_env(), scope->get_total_env())) {
				user_error(status, subtype->get_location(),
						"unable to create a sum type with %s in it. %s lacks run-time type information",
						subtype->str().c_str(),
						subtype->str().c_str());
				user_info(status, sum->get_location(),
						"while attempting to instantiate sum type %s",
						sum->str().c_str());
				break;
			}
		}

		if (!!status) {
			return bound_type;
		}
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref create_bound_function_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		const ptr<const types::type_function_t> &function)
{
	if (auto args = dyncast<const types::type_args_t>(function->args)) {
		bound_type_t::refs bound_args = upsert_bound_types(status,
				builder, scope, args->args);

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
							builder, bound_args, return_type);
					if (!!status) {
						// TODO: support dynamic function creation
						bound_type = bound_type_t::create(function,
								function->get_location(), llvm_fn_type->getPointerTo());
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
	} else {
		panic("type_args of the wrong type...");
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref create_bound_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type_t::ref type)
{
	assert(!!status);

	INDENT(3,
		string_format("attempting to create a bound type for %s in scope " c_id("%s"),
			type->str().c_str(), scope->get_name().c_str()));

    auto program_scope = scope->get_program_scope();

	if (auto id = dyncast<const types::type_id_t>(type)) {
		return create_bound_expr_type(status, builder, scope, id);
    } else if (auto maybe = dyncast<const types::type_maybe_t>(type)) {
		return create_bound_maybe_type(status, builder, scope, maybe);
	} else if (auto integer = dyncast<const types::type_integer_t>(type)) {
		return create_bound_integer_type(status, builder, scope, integer);
	} else if (auto pointer = dyncast<const types::type_ptr_t>(type)) {
		return create_bound_ptr_type(status, builder, scope, pointer);
	} else if (auto managed_type = dyncast<const types::type_managed_t>(type)) {
		return create_bound_managed_type(status, builder, scope, managed_type);
	} else if (auto struct_type = dyncast<const types::type_struct_t>(type)) {
		return create_bound_struct_type(status, builder, scope, struct_type);
	} else if (auto tuple_type = dyncast<const types::type_tuple_t>(type)) {
		return create_bound_tuple_type(status, builder, scope, tuple_type);
	} else if (auto function = dyncast<const types::type_function_t>(type)) {
		return create_bound_function_type(status, builder, scope, function);
	} else if (auto sum = dyncast<const types::type_sum_t>(type)) {
		return create_bound_sum_type(status, builder, scope, sum);
	} else if (auto operator_ = dyncast<const types::type_operator_t>(type)) {
		return create_bound_expr_type(status, builder, scope, operator_);
	} else if (auto variable = dyncast<const types::type_variable_t>(type)) {
		user_error(status, variable->get_location(), "found a free type variable where a bound type was expected: %s", variable->str().c_str());
	} else if (auto lambda = dyncast<const types::type_lambda_t>(type)) {
		user_error(status, lambda->get_location(), "unable to instantiate generic type %s without the necessary type application",
				lambda->str().c_str());
	} else if (auto ref = dyncast<const types::type_ref_t>(type)) {
		return create_bound_ref_type(status, builder, scope, ref);
	} else  if (auto extern_type = dyncast<const types::type_extern_t>(type)) {
		return create_bound_extern_type(status, builder, scope, extern_type);
	}

	assert(!status);
	return nullptr;
}

bound_type_t::ref upsert_bound_type(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
	   	types::type_t::ref type)
{
	static int depth = 0;

	depth_guard_t depth_guard(depth, 10);

	if (!!status) {
		type = type->rebind(scope->get_type_variable_bindings());

		auto signature = type->get_signature();
		auto bound_type = scope->get_bound_type(signature, false /*use_mappings*/);
		if (bound_type != nullptr) {
			/* this case is critical for breaking cycles during structure
			 * instantiation */
			return bound_type;
		} else {
			bound_type = scope->get_bound_type(signature, true /*use_mappings*/);

			if (bound_type != nullptr) {
				/* create this type's final binding */
				auto new_bound_type = bound_type_t::create(
						type,
						type->get_location(),
						bound_type->get_llvm_type(),
						bound_type->get_llvm_specific_type());

				/* this type has been mapped */
				scope->get_program_scope()->put_bound_type(status, new_bound_type);

				// TODO: consider removing the mapping?
				return new_bound_type;
			} else {
				debug_above(6, log("upsert_bound_type calling create_bound_type with %s",
							type->str().c_str()));

				/* we believe that this type does not exist. let's build it */
				bound_type = create_bound_type(status, builder, scope, type);

				if (!!status) {
					return bound_type;
				}

				user_error(status, type->get_location(),
						"unable to bind type %s in scope " c_id("%s"),
						type->str().c_str(),
						scope->get_name().c_str());
			}
		}
	}
	assert(!status);
	return nullptr;
}

bound_type_t::ref get_function_return_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::ref function_type)
{
	if (auto type_function = dyncast<const types::type_function_t>(function_type->get_type())) {
		auto return_type_sig = type_function->return_type->get_signature();
		return upsert_bound_type(status, builder, scope, type_function->return_type);
	}

	assert(!status);
	return nullptr;
}

std::pair<bound_var_t::ref, bound_type_t::ref> upsert_tuple_ctor(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		types::type_tuple_t::ref tuple_type,
		const ast::item_t::ref &node)
{
	/* this is a tuple constructor function */
	if (!!status) {
		program_scope_t::ref program_scope = scope->get_program_scope();

		bound_type_t::ref data_type = upsert_bound_type(status, builder, scope, tuple_type);

		if (!!status) {
			bound_var_t::ref tuple_ctor = get_or_create_tuple_ctor(status, builder,
					scope, data_type,
					make_iid_impl(tuple_type->repr(), tuple_type->get_location()),
					node);


			if (!!status) {
				return {tuple_ctor, data_type};
			}
		}
	}

	assert(!status);
	return {nullptr, nullptr};
}

std::pair<bound_var_t::ref, bound_type_t::ref> upsert_tagged_tuple_ctor(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		identifier::ref id,
		const ast::item_t::ref &node,
		types::type_t::ref type)
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
					scope, data_type, id, node);

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
	auto llvm_type_int32 = llvm::Type::getInt32Ty(llvm_context);
	std::vector<llvm::Constant *> llvm_gep_index = {
		llvm::ConstantInt::get(llvm_type_int32, 0),
		llvm::ConstantInt::get(llvm_type_int32, 1),
		llvm::ConstantInt::get(llvm_type_int32, index),
	};
	llvm::Constant *llvm_null_struct = llvm::Constant::getNullValue(llvm::PointerType::getUnqual(llvm_struct_type));

	debug_above(6, log(log_info, "null struct is %s", llvm_print(llvm_null_struct).c_str()));

	// llvm::Constant *llvm_null_struct = llvm::Constant::getNullValue(llvm_struct_type);
	assert(llvm_struct_type == llvm::dyn_cast<llvm::PointerType>(
				llvm_null_struct->getType()->getScalarType())->getContainedType(0u));

	llvm::Constant *llvm_gep = llvm::ConstantExpr::getInBoundsGetElementPtr(
			llvm_struct_type, llvm_null_struct, llvm_gep_index);

	return llvm::ConstantExpr::getPtrToInt(llvm_gep, llvm::Type::getInt64Ty(llvm_struct_type->getContext()));
}


bound_var_t::ref maybe_get_dtor(
		status_t &status,
		llvm::IRBuilder<> &builder,
		program_scope_t::ref program_scope,
		bound_type_t::ref data_type)
{
	INDENT(3, string_format("attempting to get a dtor for %s",
			   	data_type->get_type()->eval(program_scope, false)->str().c_str()));

	// TODO: look at what data_type is, and whether it can be passed as a raw
	// pointer.
	auto location = data_type->get_location();
	fittings_t fn_dtors;
	bound_var_t::ref dtor = maybe_get_callable(
			status,
			builder,
			program_scope,
			{"__finalize__"},
			location,
			type_args({data_type->get_type()}, {}),
			type_void(),
			fn_dtors);

	if (!!status) {
		if (dtor != nullptr) {
			return dtor;
		} else {
			// TODO: create an option to report on missing finalizers, instead of coupling it to
			// ZION_DEBUG
			debug_above(2, user_info(status, location, "no __finalize__ found for type %s",
					data_type->str().c_str()));
			return nullptr;
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref upsert_type_info_mark_fn(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		std::string name,
		location_t location,
		bound_type_t::ref data_type,
		bound_var_t::ref dtor_fn,
		bound_var_t::ref mark_fn,
		types::signature signature,
		std::string type_info_name)
{
	assert(0);
	return nullptr;
}


bound_var_t::ref upsert_type_info_offsets(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		std::string name,
		location_t location,
		bound_type_t::ref data_type,
		bound_var_t::ref dtor_fn,
		bound_type_t::refs args,
		types::signature signature,
		std::string type_info_name)
{
	llvm::Value *llvm_dtor_fn = nullptr;
	auto program_scope = scope->get_program_scope();
	bound_type_t::ref type_info_offsets = program_scope->get_runtime_type(status, builder, "type_info_offsets_t");
	if (!!status) {
		llvm::StructType *llvm_type_info_offsets_type = llvm::cast<llvm::StructType>(
				type_info_offsets->get_llvm_type());
		bound_type_t::ref type_info_head = program_scope->get_runtime_type(status, builder, "type_info_t");

		if (!!status) {
			llvm::StructType *llvm_type_info_head_type = llvm::cast<llvm::StructType>(
					type_info_head->get_llvm_type());

			llvm::Type *llvm_dtor_fn_type = llvm_type_info_offsets_type->getElementType(DTOR_FN_INDEX);

			if (dtor_fn != nullptr) {
				/* we found a dtor for this type of object */
				llvm_dtor_fn = llvm::ConstantExpr::getBitCast(
						(llvm::Constant *)dtor_fn->get_llvm_value(), llvm_dtor_fn_type);

			} else {
				/* there is no dtor, just put a NULL value in instead */
				llvm_dtor_fn = llvm::Constant::getNullValue(llvm_dtor_fn_type);
			}

			llvm::Constant *llvm_sizeof_tuple = llvm_sizeof_type(builder,
					llvm_deref_type(data_type->get_llvm_specific_type()));

			llvm::StructType *llvm_struct_type = llvm::dyn_cast<llvm::StructType>(
					llvm::dyn_cast<llvm::PointerType>(data_type->get_llvm_specific_type())
					->getElementType());

			assert(llvm_struct_type != nullptr);

			/* calculate the type map */
			std::vector<llvm::Constant *> llvm_offsets;
			llvm::Constant *llvm_dim_offsets = nullptr;

			for (size_t i=0; i<args.size(); ++i) {
				debug_above(5, log(log_info, "args[%d] is %s", i, args[i]->str().c_str()));
				bool is_managed;
				args[i]->is_managed_ptr(status, builder, program_scope, is_managed);
				if (!!status) {
					if (is_managed) {
						/* this element is managed, so let's store its memory offset in
						 * our array */
						debug_above(5, log(log_info, "getting offset of %d in %s",
									i, llvm_print(llvm_struct_type).c_str()));
						llvm_offsets.push_back(llvm::ConstantExpr::getTrunc(
									llvm_dim_offset_gep(llvm_struct_type, i),
									builder.getInt16Ty(), true));
					}
				} else {
					break;
				}
			}

			if (!!status) {
				/* now let's create a placeholder type for the dim offsets map */
				llvm::ArrayType *llvm_dim_offsets_type = llvm::ArrayType::get(
						builder.getInt16Ty(), llvm_offsets.size());

				llvm::Module *llvm_module = llvm_get_module(builder);

				if (llvm_offsets.size() != 0) {
					/* create the actual list of offsets */
					llvm::Constant *llvm_dim_offsets_raw = llvm_get_global(llvm_module,
							std::string("__dim_offsets_raw_") + name,
							llvm::ConstantArray::get(llvm_dim_offsets_type, llvm_offsets),
							true /*is_constant*/);
					debug_above(5, log(log_info, "llvm_dim_offsets_raw = %s",
								llvm_print(llvm_dim_offsets_raw).c_str()));

					llvm_dim_offsets = llvm::ConstantExpr::getBitCast(
							llvm_dim_offsets_raw, builder.getInt16Ty()->getPointerTo());

				} else {
					llvm_dim_offsets = llvm::Constant::getNullValue(builder.getInt16Ty()->getPointerTo());
				}

				debug_above(5, log(log_info, "llvm_dim_offsets = %s",
							llvm_print(llvm_dim_offsets).c_str()));

				debug_above(5, log(log_info, "mapping type " c_type("%s") " to typeid %d",
							signature.str().c_str(), atomize(signature.repr())));

				llvm::Constant *llvm_type_info_head = llvm_create_constant_struct_instance(
						llvm_type_info_head_type,
						{
						/* the type_id */
						builder.getInt32(atomize(signature.repr())),

						/* the kind of this type_info */
						builder.getInt32(type_kind_use_offsets),

						/* allocation size */
						llvm_sizeof_tuple,

						/* name this variable */
						(llvm::Constant *)builder.CreateGlobalStringPtr(name),
					});

				llvm::Constant *llvm_type_info = llvm_create_struct_instance(
					std::string("__type_info_") + signature.repr().c_str(),
					llvm_module,
					llvm_type_info_offsets_type, 
					{
						/* the base type info struct */
						llvm_type_info_head,
						
						/* finalizer */
						llvm::dyn_cast<llvm::Constant>(llvm_dtor_fn),

						/* the number of contained references */
						builder.getInt16(llvm_offsets.size()),

						/* the actual offsets to the managed references */
						llvm_dim_offsets,
					});

				auto bound_type_info_type = type_info_head->get_pointer();
				debug_above(5, log(log_info, "llvm_type_info = %s", llvm_print(llvm_type_info).c_str()));
				debug_above(5, log(log_info, "bound_type_info_type = %s", llvm_print(bound_type_info_type->get_llvm_specific_type()).c_str()));
				if (!!status) {
					auto bound_type_info_var = bound_var_t::create(
							INTERNAL_LOC(),
							type_info_name,
							bound_type_info_type,
							llvm::ConstantExpr::getPointerCast(
								llvm_type_info,
								bound_type_info_type->get_llvm_specific_type()),
							make_iid("type info value"));

					program_scope->put_bound_variable(status, type_info_name, bound_type_info_var);
					return bound_type_info_var;
				}
			}
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref upsert_type_info(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		std::string name,
		location_t location,
		bound_type_t::ref data_type,
		bound_type_t::refs args,
		bound_var_t::ref dtor_fn,
		bound_var_t::ref mark_fn)
{
	/* first check if we have already created this type info, memoized */
	auto program_scope = scope->get_program_scope();
	auto signature = data_type->get_signature();
	auto type_info_name = string_format("__type_info_%s", signature.repr().c_str());
	auto bound_type_info_var = program_scope->get_bound_variable(status, location, type_info_name);
	if (bound_type_info_var != nullptr) {
		/* we found it, let's bail */
		return bound_type_info_var;
	}

	if (mark_fn != nullptr) {
		return upsert_type_info_mark_fn(status, builder, scope, name, location, data_type, dtor_fn,
				mark_fn, signature, type_info_name);
	} else {
		return upsert_type_info_offsets(status, builder, scope, name, location, data_type, dtor_fn,
				args, signature, type_info_name);
	}
}


llvm::Value *llvm_call_allocator(
		status_t &status,
		llvm::IRBuilder<> &builder,
	   	program_scope_t::ref program_scope,
		life_t::ref life,
	   	const ast::item_t::ref &node,
		bound_type_t::ref data_type,
		bound_var_t::ref dtor_fn,
		std::string name,
		bound_type_t::refs args)
{
	debug_above(5, log(log_info, "calling allocator for %s",
				data_type->str().c_str()));

	auto bound_type_info = upsert_type_info(status, builder, program_scope,
			name, node->get_location(), data_type, args, dtor_fn, nullptr);

	if (!!status) {
		bound_var_t::ref allocation = call_program_function(
				status,
				builder,
				program_scope,
				life,
				"runtime.create_var",
				node->get_location(),
				{bound_type_info});
		if (!!status) {
			return allocation->get_llvm_value();
		}
	}
	assert(!status);
	return nullptr;
}

bound_var_t::ref get_or_create_tuple_ctor(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::ref data_type,
		identifier::ref id,
		const ast::item_t::ref &node)
{
	std::string name = id->get_name();

	auto program_scope = scope->get_program_scope();

	types::type_t::ref type = data_type->get_type();

	debug_above(4, log(log_info, "get_or_create_tuple_ctor evaluating %s with llvm type %s",
				type->str().c_str(),
				llvm_print(data_type->get_llvm_specific_type()).c_str()));
	types::type_t::ref expanded_type = type->eval(scope, true);

	types::type_product_t::ref product_type = dyncast<const types::type_product_t>(expanded_type);

	if (product_type == nullptr) {
		/* destructure the structure that this should have */
		if (auto pointer = dyncast<const types::type_ptr_t>(expanded_type)) {
			if (auto managed = dyncast<const types::type_managed_t>(pointer->element_type)) {
				product_type = dyncast<const types::type_product_t>(managed->element_type);
			} else {
				assert(false);
				return null_impl();
			}
		} else {
			assert(false);
			return null_impl();
		}
	}

	/* at this point we should have a struct type in expanded_type */
	if (product_type != nullptr) {
		types::type_args_t::ref type_args = ::type_args(types::without_refs(product_type->get_dimensions()));
		if (!!status) {
			types::type_function_t::ref function_type = ::type_function(nullptr, type_id(make_iid("true")), type_args, type);
			bound_var_t::ref already_bound_function;
			if (program_scope->has_bound(id->get_name(), function_type, &already_bound_function)) {
				/* fulfill the "get_or_" part of this function name */
				return already_bound_function;
			}

			/* save and later restore the current branch insertion point */
			llvm::IRBuilderBase::InsertPointGuard ipg(builder);

			auto function = llvm_start_function(status, builder, scope, node->get_location(), function_type, name);
			if (!!status) {
				life_t::ref life = make_ptr<life_t>(status, lf_function);

				if (!!status) {
					bound_var_t::ref dtor_fn = maybe_get_dtor(status, builder,
							program_scope, data_type);

					if (!!status) {
						types::type_args_t::ref type_args = dyncast<const types::type_args_t>(function_type->args);
						assert(type_args != nullptr);
						bound_type_t::refs args = upsert_bound_types(status, builder, scope, type_args->args);
						if (!!status) {
							llvm::Value *llvm_alloced = llvm_call_allocator(
									status, builder, program_scope, life, node, data_type,
									dtor_fn, name, args);

							if (!!status) {
								assert(data_type->get_llvm_type() != nullptr);

								/* we've allocated enough space for the object type,
								 * let's get our allocation as such */
								llvm::Value *llvm_final_obj = llvm_maybe_pointer_cast(builder,
										llvm_alloced, data_type);

								int index = 0;

								llvm::Function *llvm_function = llvm::cast<llvm::Function>(function->get_llvm_value());
								llvm::Function::arg_iterator args_iter = llvm_function->arg_begin();
								while (args_iter != llvm_function->arg_end()) {
									llvm::Value *llvm_param = &*args_iter++;
									/* get the location we should store this datapoint in */
									llvm::Value *llvm_gep = llvm_make_gep(builder, llvm_final_obj,
											index, true /* managed */);
									if (llvm_gep->getName().str().size() == 0) {
										llvm_gep->setName(string_format("address_of.member.%d", index));
									}

									debug_above(5, log(log_info, "store %s at %s", llvm_print(*llvm_param).c_str(),
												llvm_print(*llvm_gep).c_str()));
									builder.CreateStore(llvm_param, llvm_gep);

									++index;
								}

								/* create a return statement for the final object. */
								builder.CreateRet(llvm_final_obj);

								llvm_verify_function(status, id->get_location(), llvm_function);

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

bound_var_t::ref type_check_get_item_with_int_literal(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		const ast::item_t::ref &node,
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
				index_id);

		/* get or instantiate a function we can call on these arguments */
		return call_program_function(status, builder, scope, life, "__getitem__",
				node->get_location(), {lhs, index});
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
				"creating GEP for %s%s[%d]",
				managed ? "managed " : "native ",
				llvm_print(*llvm_value).c_str(), index));

#ifdef ZION_DEBUG
	auto llvm_ptr_type = llvm::dyn_cast<llvm::PointerType>(llvm_value->getType());
	assert(llvm_ptr_type != nullptr);
	auto llvm_struct_type = llvm::dyn_cast<llvm::StructType>(llvm_ptr_type->getElementType());
	assert(llvm_struct_type != nullptr);

	if (!managed) {
		llvm::Type *llvm_element_type = llvm_struct_type->elements()[index];
		assert(llvm_element_type != nullptr);
		debug_above(5, log("trying to load value for element of type %s",
					llvm_print(llvm_element_type).c_str()));
	}
#endif

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
