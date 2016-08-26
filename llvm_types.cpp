#include "zion.h"
#include "ast.h"
#include "llvm_utils.h"
#include "llvm_types.h"
#include "type_visitor.h"

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
			ptr<program_scope_t> program_scope,
			types::term::map env) :
		status(status),
		builder(builder),
		program_scope(program_scope),
		env(env)
	{
	}

	status_t &status;
	llvm::IRBuilder<> &builder;
	bound_type_t::ref created_type;
	ptr<program_scope_t> program_scope;
	types::term::map env;

	virtual bool visit(const types::type_id &id) {
		created_type = program_scope->get_bound_type(id.get_signature());
		return created_type != nullptr;
	}

	virtual bool visit(const types::type_variable &variable) {
		assert(false);
		return false;
	}

	virtual bool visit(const types::type_ref &ref) {
		if (auto type_id = dyncast<const struct ::types::type_id>(ref.macro)) {
			auto iter = env.find(type_id->id->get_name());
			if (iter != env.end()) {
				/* found a macro for this ref, let's evaluate it */
				types::type::ref evaluated_macro = iter->second->evaluate(env, 1)->get_type();

				return evaluated_macro->accept(*this);
			} else {
				return ref.macro->accept(*this);
			}
		} else {
			not_impl();
		}

		return false;
	}

	virtual bool visit(const types::type_operator &operator_) {
		assert(false);
		return false;
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
				bound_type_t::refs args = create_bound_types_from_args(status, builder, program_scope, product.dimensions[0]);
				bound_type_t::ref return_type = upsert_bound_type(status, builder, program_scope, product.dimensions[1]);
				types::type::ref fn_type = get_function_type(args, return_type);

				auto signature = fn_type->get_signature();
				created_type = program_scope->get_bound_type(signature);
				if (created_type) {
					return true;
				} else {
					auto *llvm_fn_type = llvm_create_function_type(status,
							builder, args, return_type);
					if (!!status) {
						created_type = bound_type_t::create(fn_type,
								product.get_location(), llvm_fn_type);
					}
					return !!status;
				}
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
		auto signature = sum.get_signature();
		auto bound_type = program_scope->get_bound_type(signature);
		assert(bound_type == nullptr);
		created_type = bound_type_t::create(sum.shared_from_this(), sum.get_location(),
				program_scope->get_bound_type({"__var_ref"})->llvm_type);
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

	debug_above(1, log(log_info, "creating bound type for %s", type->str().c_str()));
	bound_type_builder_t btb(status, builder,
		   	scope->get_program_scope(),
			scope->get_type_env());
	if (type->accept(btb)) {
		assert(!!status);

		return btb.created_type;
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
	if (bound_type == nullptr) {
		bound_type = create_bound_type(status, builder, scope, type);
		if (!!status) {
			scope->get_program_scope()->put_bound_type(bound_type);
		} else {
			assert(bound_type == nullptr);
		}
	}
	return bound_type;
}

bound_type_t::ref upsert_bound_type(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
	   	types::term::ref term)
{
	/* helper method to convert lambda terms to types */
	debug_above(2, log(log_info, "evaluating type term " c_term("%s"),
				term->str().c_str()));
	auto type_env = scope->get_type_env();
	auto type = term->evaluate(type_env, 0)->get_type();

	return upsert_bound_type(status, builder, scope,
			type);
}

bound_type_t::ref get_function_return_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ast::item &obj,
		scope_t::ref scope,
		bound_type_t::ref function_type)
{
	if (auto product_type = dyncast<const types::type_product>(function_type->type)) {
		assert(product_type->pk == pk_function);

		/* notice the leaky encapsulation here */
		assert(product_type->dimensions.size() == 2);

		auto return_type_sig = product_type->dimensions[1]->get_signature();

		auto return_type = scope->get_bound_type(return_type_sig);
		/* this should exist, otherwise how was the function type built in the
		 * first place */
		assert(return_type != nullptr);
		log(log_info, "got function return type %s", return_type->str().c_str());
		return return_type;
	} else {
		panic("expected a function");
		return nullptr;
	}
}

bound_type_t::ref get_or_create_tuple_type(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		atom name,
		bound_type_t::refs args,
		const ast::item::ref &node)
{
	/* get the term of this tuple type */
	types::term::ref term = get_obj_term(get_tuple_term(get_terms(args)));
	types::type::ref type = term->get_type();
	auto data_type = scope->get_bound_type(type->get_signature());

	if (data_type != nullptr) {
		return data_type;
	} else {
		auto program_scope = scope->get_program_scope();

		/* build the llvm return type */
		llvm::Type *llvm_tuple_type = llvm_create_tuple_type(
				builder, program_scope, name, args);

		/* display the new type */
		llvm::Type *llvm_obj_struct_type = llvm::cast<llvm::PointerType>(llvm_tuple_type)->getElementType();
		log(log_info, "created LLVM wrapped tuple type %s", llvm_print_type(*llvm_obj_struct_type).c_str());

		/* get the bound type of the data ctor's value */
		bound_type_t::ref data_type = bound_type_t::create(
				type,
				node->token.location,
				/* the LLVM-visible type of tuples will usually be a generic
				 * obj */
				scope->get_bound_type({"__var_ref"})->llvm_type,
				llvm_tuple_type);

		/* put the type for the data type */
		program_scope->put_bound_type(data_type);

		return data_type;
	}
}

bound_type_t::ref get_or_create_tagged_tuple_type(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		atom name,
		bound_type_t::refs args,
		const ast::item::ref &node)
{
	/* get the term of this tuple type */
	types::term::ref term = get_tuple_term(get_terms(args));
	types::type::ref type = term->get_type();
	auto data_type = scope->get_bound_type(type->get_signature());

	if (data_type != nullptr) {
		return data_type;
	} else {
		auto program_scope = scope->get_program_scope();

		/* build the llvm return type */
		llvm::Type *llvm_tuple_type = llvm_create_tuple_type(
				builder, program_scope, name, args);

		/* display the new type */
		llvm::Type *llvm_obj_struct_type = llvm::cast<llvm::PointerType>(llvm_tuple_type)->getElementType();
		log(log_info, "created LLVM wrapped type %s", llvm_print_type(*llvm_obj_struct_type).c_str());

		auto tag = ::type_product(pk_tag, {::type_id(make_iid(name.str()))});

		auto tagged_tuple_type = ::type_product(pk_tagged_tuple, {tag, type});

		/* get the bound type of the data ctor's value */
		bound_type_t::ref data_type = bound_type_t::create(
				tagged_tuple_type,
				node->token.location,
				/* the LLVM-visible type of tagged tuples will usually be a
				 * generic obj */
				scope->get_bound_type({"__var_ref"})->llvm_type,
				llvm_tuple_type);

		/* put the type for the data type */
		program_scope->put_bound_type(data_type);

		return data_type;
	}
}

std::pair<bound_var_t::ref, bound_type_t::ref> instantiate_tuple_ctor(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::refs args,
		atom name,
		const location &location,
		const ast::item::ref &node)
{
	/* this is a tuple constructor function */
	std::vector<llvm::Type*> llvm_parameter_types;

	for (auto &arg : args) {
		llvm_parameter_types.push_back(arg->llvm_type);
	}

	if (!!status) {
		program_scope_t::ref program_scope = scope->get_program_scope();

		bound_type_t::ref data_type = get_or_create_tuple_type(builder, scope, name, args, node);

		bound_var_t::ref tuple_ctor = get_or_create_tuple_ctor(status, builder,
				scope, args, data_type, name, location, node);

		if (!!status) {
			return {tuple_ctor, data_type};
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
		atom name,
		const location &location,
		const ast::item::ref &node)
{
	/* this is a tuple constructor function */
	std::vector<llvm::Type*> llvm_parameter_types;

	for (auto &arg : args) {
		llvm_parameter_types.push_back(arg->llvm_type);
	}

	if (!!status) {
		program_scope_t::ref program_scope = scope->get_program_scope();

		bound_type_t::ref data_type = get_or_create_tagged_tuple_type(builder, scope, name, args, node);

		bound_var_t::ref tagged_tuple_ctor = get_or_create_tuple_ctor(status, builder,
				scope, args, data_type, name, location, node);

		if (!!status) {
			return {tagged_tuple_ctor, data_type};
		}
	}

	assert(!status);
	return {nullptr, nullptr};
}

std::pair<bound_var_t::ref, bound_type_t::ref> instantiate_struct_ctor(
		status_t &status, 
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::ref struct_type,
		bound_type_t::refs dim_types,
		atom name,
		const location &location,
		const ast::item::ref &node)
{
	/* this is a struct constructor function.
	 * note that structs and tuples are the same thing internally to LLVM,
	 * however at our type-checking level, we'll want to be able to lookup
	 * named data points within tuples. this requires a more complete
	 * term, but results in a similar IR. */
	std::vector<llvm::Type*> llvm_parameter_types;

	for (auto &type : dim_types) {
		llvm_parameter_types.push_back(type->llvm_type);
	}

	if (!!status) {
		program_scope_t::ref program_scope = scope->get_program_scope();

		bound_var_t::ref struct_ctor = get_or_create_tuple_ctor(status, builder,
				scope, dim_types, struct_type, name, location, node);

		if (!!status) {
			return {struct_ctor, struct_type};
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
		atom name,
		const location &location,
		const ast::item::ref &node)
{
	if (auto var = scope->maybe_get_bound_variable(name)) {
		user_error(status, *node, "the name " c_id("%s") " is taken. see %s",
				name.c_str(),
				var->str().c_str());
		return nullptr;
	}

	auto program_scope = scope->get_program_scope();

	/* save and later restore the current branch insertion point */
	llvm::IRBuilderBase::InsertPointGuard ipg(builder);
	auto function = llvm_start_function(status, builder, scope,
			node, args, data_type, name);

	if (!!status) {
		bound_var_t::ref mem_alloc_var = program_scope->get_bound_variable(status, node, "__create_var");

		assert(!!status);
		assert(mem_alloc_var != nullptr);

		llvm::Value *llvm_sizeof_tuple = llvm_sizeof_type(builder, llvm_deref_type(data_type->llvm_type));

		llvm::Value *llvm_create_var_call_value = llvm_create_call_inst(
				status, builder, *node,
				mem_alloc_var,
				{
					/* name this variable */
					builder.CreateGlobalStringPtr(name.str()),

					/* no mark function yet */
					llvm::Constant::getNullValue(
							program_scope->get_bound_type({"__mark_fn"})->llvm_type),

					/* the type_id */
					builder.getInt32(function->type->get_signature().repr().iatom),

					/* allocation size */
					llvm_sizeof_tuple
				});

		assert(data_type->llvm_specific_type != nullptr);

		/* we've allocated enough space for the object type, let's get our allocation as such */
		llvm::Value *llvm_final_obj = builder.CreatePointerBitCastOrAddrSpaceCast(
				llvm_create_var_call_value, 
				data_type->llvm_specific_type);

		int index = 0;

		llvm::Function *llvm_function = (llvm::Function *)function->llvm_value;
		llvm::Function::arg_iterator args_iter = llvm_function->arg_begin();
		while (args_iter != llvm_function->arg_end()) {
			llvm::Value *llvm_param = args_iter++;
			/* get the location we should store this datapoint in */
			llvm::Value *llvm_gep = builder.CreateInBoundsGEP(llvm_final_obj,
					{builder.getInt32(0), builder.getInt32(1), builder.getInt32(index++)});
			log(log_info, "store %s at %s", llvm_print_value(*llvm_param).c_str(),
					llvm_print_value(*llvm_gep).c_str());
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
			scope->put_bound_variable(name, function);

			log(log_info, "module so far is:\n" c_ir("%s"), llvm_print_module(
						*llvm_get_module(builder)).c_str());
			return function;
		}
	}
	assert(!status);
	return nullptr;
}

#if 0
	types::term::ref lazy_term = (term);
	llvm::Type *llvm_sum_type = llvm_create_sum_type(
			builder, scope->get_program_scope(), term.repr());

	/* the base type is a lazy type that we will fill in later, but it has a
	 * concrete LLVM type which is simply a pointer. think C structure fwd
	 * decls with pointers */
	auto base_type = bound_type_t::create(lazy_term,
				llvm_sum_type,
				shared_from_this());

	/* make sure any references in child data ctors have something to refer to
	 * at instantiation */
	scope->put_bound_type(base_type);

	/* go through all of our data ctors, instantiating their functions. also, keep
	 * track of whether they are generic */
	bool fully_bound = true;
	for (int i = 0; i < data_ctors.size(); ++i) {
		bool ctor_bound = false;
		/* for each ctor in this type_sum, let's generate the appropriate types
		 * and ctor functions */
		bind_ctor_to_scope(status, builder, scope, data_ctors[i], ctor_bound);

		if (!!status) {
			/* keep track of whether we've got all of data ctors bound */
			if (!ctor_bound) {
				/* we can't have an unbound ctor if we don't have type parameters to our
				 * type_def */
				assert(term.args.size() > 0);
				fully_bound = false;
			}
		}
	}

	if (!!status) {
		assert_implies(!fully_bound, term.args.size() > 0);

		// TODO: after we've instantiated or found all our data types, let's
		// create a term which treats them all. We'll build an inverted
		// could-be-a graph from the base type to the descendant types. Or, we
		// don't need to do this because at the point of use it will all become
		// clear...
		return base_type;
	}
#endif

#if 0
	atom::many dim_names;
	std::vector<bound_type_t::ref> dim_types;
	types::term::refs dim_terms;

	for (auto &dimension : dimensions) {
		bound_type_t::ref dim_type = dimension->type_ref->resolve_type(
				status, builder, scope, nullptr, nullptr);

		if (!!status) {
			/* pull out info per dimension */
			dim_names.push_back(dimension->name);
			dim_types.push_back(dim_type);
			dim_terms.push_back(dim_type->term);
		}
	}

	if (!!status) {
		assert(dim_names.size() == dim_types.size());

		/* construct a struct term */
		types::term::ref data_term = get_obj_term(
				get_struct_term(dim_names, dim_terms));

		llvm::Type *llvm_struct_type = llvm_create_tuple_type(builder,
				scope->get_program_scope(),
				string_format("product_type!%s", data_term.repr().c_str()),
				dim_types);

		/* display the new type */
		llvm::Type *llvm_obj_struct_type = llvm::cast<llvm::PointerType>(llvm_struct_type)->getElementType();
		log(log_info, "created LLVM wrapped type %s", llvm_print_type(*llvm_obj_struct_type).c_str());

		/* get the bound type of the struct ctor's value */
		auto struct_type = bound_type_t::create(data_term,
				llvm_struct_type, shared_from_this());

		/* add this struct's term type to the program types list */
		auto program_scope = scope->get_program_scope();
		program_scope->put_bound_type(struct_type);

		/* create the ctor for this product type */
		instantiate_struct_ctor(status, builder, scope, struct_type, dim_types,
				term.name, token.location, shared_from_this());

		return struct_type;
	}
#endif

types::term::ref ast::type_alias::instantiate_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		atom::many type_variables,
	   	scope_t::ref scope) const
{
	log(log_info, "creating type alias term for %s", str().c_str());

	assert(type_variables.size() == 0);
	return null_impl(); // type_ref->resolve_type(status, builder, scope, nullptr, nullptr);
}

bound_var_t::ref call_const_subscript_operator(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		const ast::item::ref &node,
		bound_var_t::ref lhs,
		int subscript_index)
{
#if 0
	if (subscript_index < 0) {
		user_error(status, *node, "constant subscripts must be positive");
	} else {
		/* do some checks on the lhs */
		types::term::ref lhs_term = lhs->type->term;
		if (!lhs_term.is_obj()) {
			user_error(status, *node, "subscript operator only works on objects for now");
		} else {
			if (lhs_term.args.size() != 1 || !lhs_term.args[0].is_tuple()) {
				user_error(status, *node, "subscript operator only works on tuples for now");
			} else {
				/* actually create the subscript operator */
				types::term::ref tuple_sig = lhs_term.args[0];
				if (tuple_sig.args.size() <= subscript_index) {
					user_error(status, *node, "subscript is out of range [%d]. last item is [%d]",
							subscript_index, tuple_sig.args.size() - 1);
				} else {
					/* ok, we're in range */
					bound_type_t::ref data_type = scope->maybe_get_bound_type(tuple_sig.args[subscript_index]);

					if (data_type) {
						llvm::Value *llvm_lhs = llvm_resolve_alloca(builder, lhs->llvm_value);
						log(log_info, "creating GEP for %s", llvm_print_value(*llvm_lhs).c_str());
						llvm::Value *llvm_gep = builder.CreateInBoundsGEP(llvm_lhs,
								{builder.getInt32(0), builder.getInt32(1), builder.getInt32(subscript_index)});
						llvm::Value *llvm_value = builder.CreateLoad(llvm_gep);
						return bound_var_t::create(
								INTERNAL_LOC(),
								"temp_deref_subscript",
								data_type,
								llvm_value,
								node);
					} else {
						user_error(status, *node, "could not determine the type of %s",
								tuple_sig.args[subscript_index].str().c_str());
					}
				}
			}
		}
	}
#endif

	assert(!status);
	return nullptr;
}
