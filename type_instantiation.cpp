#include "zion.h"
#include <iostream>
#include <numeric>
#include <list>
#include "type_instantiation.h"
#include "bound_var.h"
#include "scopes.h"
#include "ast.h"
#include "llvm_types.h"
#include "llvm_utils.h"
#include "phase_scope_setup.h"
#include "types.h"
#include "code_id.h"

/* When we encounter the Empty declaration, we have to instantiate something.
 * When we create Empty() with term __obj__{__tuple__}. We don't bother
 * associating anything with the base type. We also create a bound type with
 * term 'Empty' that just maps to the raw __obj__{__tuple__} one.
 *
 * When we encounter Just, we create an unchecked data ctor, which would look
 * like:
 *
 *     def Just(any X) Just{any X}
 *
 * if it needed to have an AST. And, importantly, we do not create a type for
 * Just yet because it's not fully bound.
 * 
 * When we encounter a bound instance of the base type, like:
 * 
 *     var m Maybe{int} = ...
 *
 * we instantiate all the data ctors that are not yet instantiated.
 *
 * In the case of self-references like:
 *
 * type IntList is Node(int, IntList) or Done
 *
 * We notice that the base type is not parameterized. So, we immediately create
 * the base sum type IntList that maps to term __or__{Node{int, IntList},
 * Done} where the LLVM representation of this is just a raw var_t pointer that
 * can later be upcast, based on pattern matching on the type_id.
 */

void resolve_type_ref_params(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		types::type::refs type_args,
		bound_type_t::refs &args)
{
	if (!!status) {
		/* get the parameter list */
		for (auto &type_arg : type_args) {
			bound_type_t::ref bound_param_type = upsert_bound_type(
					status, builder, scope, type_arg);

			if (!!status) {
				/* keep track of this parameter */
				args.push_back(bound_param_type);
			}
		}
	}
}

bound_var_t::ref bind_ctor_to_scope(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		identifier::ref id,
		ast::item::ref node,
		types::type::refs args_types,
		types::type::ref return_type,
		atom::map<int> member_index)
{
	bool is_instantiation = bool(dyncast<generic_substitution_scope_t>(scope));
	assert(is_instantiation);

	/* create or find an existing ctor function that satisfies the term of
	 * this node */
	debug_above(5, log(log_info, "finding/creating data ctor for " c_type("%s") " with return type %s",
			id->str().c_str(), return_type->str().c_str()));

	bound_type_t::refs args;
	resolve_type_ref_params(status, builder, scope, args_types, args);

	if (!!status) {
		/* now that we know the parameter types, let's see what the term looks like */
		debug_above(5, log(log_info, "ctor term should be %s -> %s",
					::str(args_types).c_str(), return_type->str().c_str()));

		/* now we know the term of the ctor we want to create. let's check
		 * whether this ctor already exists. if so, we'll just return it. if not,
		 * we'll generate it. */
		auto tuple_pair = instantiate_tagged_tuple_ctor(status, builder, scope,
				args, member_index, id, node, return_type);

		if (!!status) {
			debug_above(5, log(log_info, "created a ctor %s", tuple_pair.first->str().c_str()));
			return tuple_pair.first;
		}
	}

	assert(!status);
	return nullptr;
}

void ast::type_product::register_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		identifier::ref supertype_id,
		identifier::refs type_variables,
		scope_t::ref scope) const
{
	debug_above(5, log(log_info, "creating product type term for %s", str().c_str()));

	atom::map<int> member_index;
	types::term::refs term_dimensions;
	int index = 0;
	for (auto dimension : dimensions) {
		term_dimensions.push_back(dimension->type_ref->get_type_term(type_variables));
		member_index[dimension->name] = index++;
	}

	/* Note that product types do not have a named super type. And, this node
	 * is a type algebra node, therefore, it's token points to "has", instead
	 * of the actual typename we're trying to create. So, we use the given
	 * supertype_id as the name for the data ctor. */
	register_data_ctor(status, builder,
			type_variables, scope, shared_from_this(),
			term_dimensions,
			member_index,
			supertype_id /*id*/,
			nullptr /*supertype_id*/);
}

void ast::type_sum::register_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		identifier::ref supertype_id,
		identifier::refs type_variables,
		scope_t::ref scope) const
{
	debug_above(3, log(log_info, "creating subtypes to %s with type variables [%s]",
				token.text.c_str(),
				join(type_variables, ", ").c_str()));

	types::term::refs subtypes_terms;
	for (auto subtype : subtypes) {
		subtypes_terms.push_back(subtype->get_type_term(type_variables));
		// TODO: register the subtype -> supertype mapping in the type env for
		// this subtype.
	}

	types::term::ref term_sum = types::term_sum(subtypes_terms);
	for (auto iter=type_variables.rbegin();
			iter != type_variables.rend();
			++iter)
	{
		term_sum = types::term_lambda(*iter, term_sum);
	}

	/* register the type declaration of this sum type. */
	types::term::ref term_sum_binder = types::term_sum_binder(builder, scope,
			types::term_id(supertype_id), shared_from_this(), term_sum);

	scope->put_type_decl_term(supertype_id->get_name(), term_sum_binder);
}

types::term::ref instantiate_data_ctor_type_term(
		status_t &status,
		llvm::IRBuilder<> &builder,
		identifier::refs type_variables,
		scope_t::ref scope,
		ptr<const ast::item> node,
		types::term::refs dimensions,
		atom::map<int> member_index,
		identifier::ref id,
		identifier::ref supertype_id)
{
	atom tag_name = id->get_name();
	auto tag_term = types::term_id(id);

	/* create a term that takes the used type variables in the data ctor and
	 * returns placement in given type variable order */
	/* instantiate the necessary components of a data ctor */
	atom::set generics = to_atom_set(type_variables);

	/* ensure that there are no duplicate type variables */
	assert(generics.size() == type_variables.size());

	auto product = types::term_product(pk_tuple, dimensions);
	debug_above(5, log(log_info, "setting up data ctor for " c_id("%s") " with value type %s",
					tag_name.c_str(), product->str().c_str()));

	/* figure out what type names are referenced in the data ctor's dimensions */
	atom::set unbound_vars = product->unbound_vars();

	/* if any of the type names are actually inbound type variables, take note
	 * of the order they are mentioned. this loop is important. it is
	 * calculating what this data ctor's supertype expansion will be. that is,
	 * it tells us how to create the lambda we'll place into the type
	 * environment to represent the fact that this data ctor is a subtype of
	 * the supertype. and, it tells us which types are parametrically bound to
	 * this subtype, and which are still quantified */

	/* lambda_vars tracks the order of the lambda variables we'll accept as we abstract our
	 * supertype expansion */
	std::list<identifier::ref> lambda_vars;

	/* supertype_expansion_list tracks the total set of parameters that the
	 * supertype expects */
	types::term::refs supertype_expansion_list;

	for (auto type_var : type_variables) {
		if (in(type_var->get_name(), unbound_vars)) {
			/* this variable is referenced by the current data ctor (the
			 * subtype), therefore it has opinions about its role in the
			 * supertype */
			lambda_vars.push_front(type_var);
			supertype_expansion_list.push_back(types::term_id(type_var));
		} else {
			/* this variable is not referenced by the current data ctor (the
			 * subtype), therefore it has no opinions about its role in the
			 * supertype */
			supertype_expansion_list.push_back(types::term_generic());
		}
	}

	if (supertype_id != nullptr) {
		/* now let's create the abstraction */
		auto supertype_expansion = types::term_id(supertype_id);
		for (auto e : supertype_expansion_list) {
			supertype_expansion = types::term_apply(supertype_expansion, e);
		}
		for (auto lambda_var : lambda_vars) {
			supertype_expansion = types::term_lambda(lambda_var, supertype_expansion);
		}
		scope->put_type_term(tag_name, supertype_expansion);
	}

	/* let's create the return type that will be the codomain of the ctor fn */
	auto data_ctor_term = tag_term;
	for (auto lambda_var : lambda_vars) {
		data_ctor_term = types::term_apply(data_ctor_term, types::term_generic(lambda_var));
	}
	debug_above(5, log(log_info, "data_ctor_term = %s", data_ctor_term->str().c_str()));

	// TODO: check whether "generics" is too heavy-handed, might be able to
	// subtract variables that are not part of the unbound variables.
	auto dequantified_product = product->dequantify(generics);
	auto dequantified_data_ctor_term = data_ctor_term->dequantify(generics);

	auto data_ctor_sig = get_function_term(
			types::change_product_kind(pk_args, dequantified_product),
		   	dequantified_data_ctor_term);

	for (auto lambda_var : lambda_vars) {
		data_ctor_sig = types::term_lambda(lambda_var, data_ctor_sig);
	}

	/* we need to register the type decl definition, so that in case we need to
	 * instantiate it, we can */
	types::term::refs type_ctor_terms;
	types::term::ref term_binder = types::term_binder(builder, scope, id, node,
			data_ctor_sig, member_index);

	debug_above(6, log(log_info, "created term_binder %s", term_binder->str().c_str()));
	scope->put_type_decl_term(tag_name, term_binder);

	if (dimensions.size() == 0) {
		/* it's a nullary enumeration or "tag", let's create a global value to represent
		 * this tag. */

		/* enum values must have a supertype, right? */
		assert(supertype_id != nullptr);

		auto tag_type = tag_term->get_type(status);
		if (!!status) {
			/* start by making a type for the tag */
			bound_type_t::ref bound_tag_type = bound_type_t::create(
					tag_type,
					id->get_location(),
					/* all tags use the var_t* type */
					scope->get_program_scope()->get_bound_type({"__var_ref"})->get_llvm_type());

			bound_var_t::ref tag = llvm_create_global_tag(
					builder, scope, bound_tag_type, tag_name, id);

			/* record this data ctor for use later */
			scope->put_bound_variable(tag_name, tag);

			debug_above(7, log(log_info, "instantiated nullary data ctor %s",
						tag->str().c_str()));
		}
	}

	if (!!status) {
		/* now let's make sure we register this constructor as an override for
		 * the name `tag_name` */
		debug_above(2, log(log_info, "adding %s as an unchecked generic data_ctor",
					id->str().c_str()));

		if (auto module_scope = dyncast<module_scope_t>(scope)) {
			types::term::ref generic_args = types::change_product_kind(pk_args, product);

			debug_above(5, log(log_info, "reduced to %s", generic_args->str().c_str()));
			types::term::ref data_ctor_sig = get_function_term(generic_args, data_ctor_term);

			assert(id->get_name() == tag_name);
			/* side-effect: create an unchecked reference to this data ctor into
			 * the current scope */
			module_scope->put_unchecked_variable(tag_name,
					unchecked_data_ctor_t::create(id, node,
						module_scope, data_ctor_sig, member_index));

			return dequantified_data_ctor_term;
		} else {
			user_error(status, node->token.location, "local type definitions are not yet impl");
		}
	}

	assert(!status);
	return nullptr;
}

#if 0
types::term::ref ast::data_ctor::instantiate_type_term(
		status_t &status,
		llvm::IRBuilder<> &builder,
		identifier::ref supertype_id,
		identifier::refs type_variables,
		scope_t::ref scope) const
{
	debug_above(5, log(log_info, "creating sum type term for %s", str().c_str()));

	types::term::refs dimensions;
	for (auto type_ref : type_ref_params) {
		dimensions.push_back(type_ref->get_type_term());
	}
	auto id = make_code_id(token);

	return register_data_ctor(status, builder,
			type_variables, scope, shared_from_this(),
			dimensions, {} /*member_index*/, id, supertype_id);
}
#endif

types::term::ref register_data_ctor(
		status_t &status,
		llvm::IRBuilder<> &builder,
		identifier::refs type_variables,
		scope_t::ref scope,
		ptr<const ast::item> node,
		types::term::refs dimensions,
		atom::map<int> member_index,
		identifier::ref id,
		identifier::ref supertype_id)
{
	atom name = id->get_name();
	auto location = id->get_location();
	if (supertype_id == nullptr || (supertype_id->get_name() != id->get_name())) {
		if (auto found_type = scope->get_bound_type(id->get_name())) {
			/* simple check for an already bound monotype */
			user_error(status, location, "symbol " c_id("%s") " was already defined",
					name.c_str());
			user_message(log_warning, status, found_type->get_location(),
					"previous version of %s defined here",
					found_type->str().c_str());
		} else {
			auto env = scope->get_type_env();
			auto env_iter = env.find(name);
			if (env_iter != env.end()) {
				/* simple check for an already bound type env variable */
				user_error(status, location,
					   	"symbol " c_id("%s") " is already taken in type env by %s",
						name.c_str(),
						env_iter->second->str().c_str());
			} else {
				var_t::refs fns;
				scope->get_callables(name, fns);
				if (fns.size() != 0) {
					user_error(status, location,
						   	"symbol " c_id("%s") " is already registered as a callable",
							name.c_str());
					for (auto fn : fns) {
						user_message(log_warning, status, fn->get_location(),
								"previous callable named %s defined here",
								fn->str().c_str());
					}
				} else {
					return instantiate_data_ctor_type_term(status, builder,
							type_variables, scope, node, dimensions,
							member_index, id, supertype_id);
				}
			}
		}
	} else {
		user_error(status, location,
			   	"data constructors cannot be named the same as their supertype");
	}

	assert(!status);
	return nullptr;
}
