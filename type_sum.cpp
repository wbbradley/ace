#include "zion.h"
#include "bound_var.h"
#include "scopes.h"
#include "ast.h"
#include "llvm_types.h"
#include "llvm_utils.h"
#include "phase_scope_setup.h"
#include "types.h"
#include "code_id.h"
#include <numeric>

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
		ast::data_ctor::ref data_ctor,
		bound_type_t::refs &args,
		const atom::set &type_variables,
		bool &found_generic)
{
	if (!!status) {
		/* get the parameter list */
		for (auto &type_ref: data_ctor->type_ref_params) {
			types::term::ref param_sig = type_ref->get_type_term();
			auto type_env = scope->get_type_env();

			types::type::ref param_type = param_sig->evaluate(
					type_env, 0 /*macro_depth*/)->get_type();

			bool fully_bound = param_type->ftv() == 0;
			if (fully_bound) {
				bound_type_t::ref bound_param_type = upsert_bound_type(
						status, builder, scope, param_type);

				if (!!status) {
					/* keep track of this parameter */
					args.push_back(bound_param_type);
				}
			} else {
				found_generic = true;
			}
		}
	}
}

bound_var_t::ref bind_ctor_to_scope(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		ast::data_ctor::ref data_ctor,
		bool &fully_bound)
{
#if 0
	bool is_instantiation = bool(dyncast<generic_substitution_scope_t>(scope));

	if (auto type = scope->maybe_get_bound_type({data_ctor->token.text})) {
		if (!type->term.is_data_ctor()) {
			user_error(status, *data_ctor,
					"the type " c_type("%s") " already exists and is not a valid data constructor (%s)",
					data_ctor->token.text.c_str(),
					type->str().c_str());
		} else {
			// TODO: consider when the type already exists
			not_impl();
		}
	} else {
		/* create or find an existing ctor function that satisfies the term of
		 * this data_ctor */
		log(log_info, "finding/creating data ctor for " c_type("%s"),
				data_ctor->token.str().c_str());

		bound_type_t::refs args;
		bool found_generic = false;
		resolve_type_ref_params(status, builder, scope, data_ctor, args,
				is_instantiation ? atom::set{} : data_ctor->type_variables,
				found_generic);

		if (!!status) {
			if (found_generic) {
				fully_bound = false;

				/* we know that the data ctor we were supposed to create is not
				 * fully bound, so we will need to create a generic ctor
				 * function, and register it with unchecked variables. since
				 * the base type knows its descendents (by term identity),
				 * it's fine if the instantiation of these functions and types
				 * happen at different times, though perhaps a little magical
				 * or confusing. */

				auto module_scope = dyncast<module_scope_t>(scope);
				assert(module_scope != nullptr);

				log(log_info, "adding %s as an unchecked generic data_ctor",
						data_ctor->token.str().c_str());
				scope_setup_function_defn(status, *data_ctor,
						data_ctor->token.text, module_scope);
				return nullptr;
			} else {
				fully_bound = true;
			}

			types::term::ref data_term = get_obj_term(get_tuple_term(args));

			/* now that we know the parameter types, let's see what the term looks like */
			types::term::ref ctor_term = get_function_term(
					args, data_term);

			log(log_info, "ctor term should be %s", ctor_term->str().c_str());

			/* now we know the term of the ctor we want to create. let's check
			 * whether this ctor already exists. if so, we'll just return it. if not,
			 * we'll generate it. */

			var_t::refs fns;
			bound_var_t::ref ctor_fn;
			if (!is_instantiation) {
				/* if we're not already trying to instantiate this guy, check
				 * whether it already existis. otherwise, we'll loop forever. */
				ctor_fn = maybe_get_callable(
					status, builder, scope, data_ctor->token.text, data_ctor,
					get_args_term(args), fns);
			}

			/* we found a match in terms of parameters */
			if (ctor_fn != nullptr) {
				log(log_info, "found a matching ctor %s", ctor_fn->str().c_str());
				if (!ctor_fn->type->term.is_data_ctor()) {
					user_error(status, *data_ctor,
							"the function " c_var("%s") " already exists and is not a valid data constructor",
							ctor_fn->name.c_str());
					return nullptr;
				}
			} else {
				auto tuple_pair = instantiate_tuple_ctor(status, builder, scope,
						args, data_ctor->token.text, data_ctor->token.location, data_ctor);
				if (!!status) {
					ctor_fn = tuple_pair.first;
					log(log_info, "created a ctor %s", ctor_fn->str().c_str());
				}
			}

			if (!!status) {
				return ctor_fn;
			}
		}
	}
#endif

	assert(!status);
	return nullptr;
}

types::term::ref ast::type_product::instantiate_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		atom::many type_variables,
		scope_t::ref scope) const
{
	log(log_info, "creating product type term for %s", str().c_str());

	types::term::refs term_dimensions;
	for (auto dimension : dimensions) {
		auto term_dim = types::term_product(pk_named_dimension,
				{
					/* the "member" variable name */
					types::term_id(make_code_id(dimension->token)),

					/* the "member" variable type term */
					dimension->type_ref->get_type_term()
				});
		term_dimensions.push_back(term_dim);
	}

	assert(type_variables.size() == 0);
	return types::term_product(pk_struct, term_dimensions);
}

types::term::ref ast::type_sum::instantiate_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		atom::many type_variables,
		scope_t::ref scope) const
{
	log(log_info, "creating sum type term with type variables [%s] that %s",
		   join(type_variables, ", ").c_str(),
		   str().c_str());

	types::term::refs options;
	for (auto data_ctor : data_ctors) {
		options.push_back(data_ctor->instantiate_type_term(status, builder,
					type_variables, scope));
	}

	identifier::refs ids;
	for (auto type_var : type_variables) {
		ids.push_back(make_iid(type_var));
	}

	return std::accumulate(ids.rbegin(), ids.rend(),
			types::term_sum(options), types::term_lambda_reducer);
}

types::term::ref ast::data_ctor::instantiate_type_term(
		status_t &status,
		llvm::IRBuilder<> &builder,
		atom::many type_variables,
		scope_t::ref scope) const
{
	types::term::refs dimensions;
	for (auto type_ref : type_ref_params) {
		dimensions.push_back(type_ref->get_type_term());
	}

	auto id = make_code_id(token);
	atom tag_name = {token.text};
	auto tag_term = types::term_product(pk_tag, {types::term_id(id)});

	if (dimensions.size() == 0) {
		/* it's a nullary enumeration or "tag", let's create a global value to represent
		 * this tag. */

		/* start by making a type for the tag */
		bound_type_t::ref tag_type = bound_type_t::create(
				tag_term->get_type(),
				token.location,
				/* all tags use the var_t* type */
				scope->get_program_scope()->get_bound_type({"__var_ref"})->llvm_type);

		bound_var_t::ref tag = llvm_create_global_tag(
				builder, scope, tag_type, tag_name, id);

		/* record this data ctor for use later */
		scope->put_bound_variable(tag_name, tag);

		debug_above(7, log(log_info, "instantiated nullary data ctor %s",
				tag->str().c_str()));

		/* all we need is a tag */
		return tag_term;
	} else {
		/* instantiate the necessary components of a data ctor */
		atom::set type_vars;
		type_vars.insert(type_variables.begin(), type_variables.end());

		/* ensure that there are no duplicate type variables */
		assert(type_vars.size() == type_variables.size());

		auto product = types::term_product(pk_tuple, dimensions);
		atom::set unbound_vars = product->unbound_vars();

		/* find the type variables that are referenced within the unbound
		 * vars of the product. */
		atom::many referenced_type_variables;
		for (auto type_variable : type_variables) {
			if (unbound_vars.find(type_variable) != unbound_vars.end()) {
				referenced_type_variables.push_back(type_variable);
			}
		}

		/* let's create the macro body for this data ctor's type and insert it
		 * into the env first */
		auto data_ctor_term = types::term_product(pk_tagged_tuple, {tag_term, product});
		auto macro_body = data_ctor_term;

		/* fold lambda construction for the type variables that are unbound
		 * from right to left around macro_body. */
		for (auto iter_var = referenced_type_variables.rbegin();
				iter_var != referenced_type_variables.rend();
				iter_var++)
		{
			macro_body = types::term_lambda(make_iid(*iter_var), macro_body);
		}

		/* place the macro body into the environment for this data_ctor type */
		// TODO: consider namespacing here
		scope->put_type_term(tag_name, macro_body);

		/* construct a reference to the macro invocation like:
		 * (ref macro-name args...) where args is the list of unbound type
		 * variables in order and return that. */

		types::term::refs term_ref_args;
		for (auto referenced_type_variable : referenced_type_variables) {
			term_ref_args.push_back(types::term_id(make_iid(referenced_type_variable)));
		}

		/* now let's make sure we register this constructor as an override for
		 * the name `tag_name` */
		debug_above(2, log(log_info, "adding %s as an unchecked generic data_ctor",
				token.str().c_str()));

		auto module_scope = dyncast<module_scope_t>(scope);
		assert(module_scope != nullptr);

		/* compute the placement of the known type variables by performing as
		 * many beta-reductions as necessary using the type variables' generic
		 * forms as operands */
		auto var_dims = product;
		for (auto referenced_type_variable : referenced_type_variables) {
			auto id = make_iid(referenced_type_variable);
			var_dims = types::term_apply(types::term_lambda(id, var_dims), types::term_generic(id));
		}
		debug_above(5, log(log_info, "injecting type generics into %s",
					var_dims->str().c_str()));

		var_dims = var_dims->evaluate({}, 0 /*macro_depth*/);

		types::term::ref generic_args = types::change_product_kind(pk_args, var_dims);

		debug_above(5, log(log_info, "reduced to %s", var_dims->str().c_str()));

		types::term::ref data_ctor_sig = get_function_term(
				generic_args,
				data_ctor_term);

		/* side-effect: create an unchecked reference to this data ctor into the current scope */
		module_scope->put_unchecked_variable(tag_name,
				unchecked_data_ctor_t::create(tag_name, shared_from_this(),
					module_scope, data_ctor_sig));

		return types::term_ref(
				types::term_id(make_iid(tag_name)),
				term_ref_args);
	}
}
