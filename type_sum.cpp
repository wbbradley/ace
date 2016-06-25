#include "zion.h"
#include "bound_var.h"
#include "scopes.h"
#include "ast.h"
#include "llvm_types.h"
#include "phase_scope_setup.h"
#include "types.h"

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


