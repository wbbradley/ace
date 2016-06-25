#include "zion.h"
#include "dbg.h"
#include "unchecked_var.h"
#include "ast.h"
#include "bound_type.h"

std::string unchecked_var_t::str() const {
   	types::term::ref sig = get_term();
    std::stringstream ss;
    ss << name << " : " << node->token.str() << " : " << sig;
    return ss.str();
}

types::term::ref unchecked_var_t::get_term() const {
	if (auto fn = dyncast<const ast::function_defn>(node)) {
		auto decl = fn->decl;
		assert(decl != nullptr);

		if (decl->param_list_decl != nullptr) {
			/* this is a function declaration, so let's set up our output parameters */
			types::term::refs args;

			/* get the parameters */
			auto &params = decl->param_list_decl->params;
			for (auto &param : params) {
				if (!param->type_ref) {
					args.push_back(types::term_generic());
				} else {
					args.push_back(param->type_ref->get_type_term());
				}
			}

			if (decl->return_type_ref) {
				/* get the return type term */
				types::term::ref sig = get_function_term(
						get_args_term(args),
						decl->return_type_ref->get_type_term());

				log(log_info, "found unchecked term for %s : %s",
						decl->token.str().c_str(),
						sig->str().c_str());
				return sig;
			} else {
				/* default to void, which is fully bound */
				types::term::ref sig = get_function_term(
						get_args_term(args),
					   	types::term_unreachable());

				debug_above(4, log(log_info, "defaulting return type of %s to void : %s",
							decl->token.str().c_str(),
							sig->str().c_str()));
				return sig;
			}
		} else {
			panic("function declaration has no parameter list");
			return types::term_unreachable();
		}
	} else if (auto data_ctor = dyncast<const ast::data_ctor>(node)) {
		/* we need to construct a term to unify for the data ctor */

		/* this is a data ctor declaration, so let's set up our input
		 * parameters */
		types::term::refs args;

		/* get the parameters */
		auto &type_ref_params = data_ctor->type_ref_params;
		for (auto &type_ref : type_ref_params) {
			args.push_back(type_ref->get_type_term());
		}

		/* get the data ctor's return type term */
		types::term::ref data_ctor_sig = types::term_product(
				pk_obj, {get_tuple_term(args)});

		types::term::ref sig = get_function_term(
				get_args_term(args),
				data_ctor_sig);

		/* now, let's make sure we universally quantify the type variables */
		types::term::ref sig_with_subs = null_impl(); // quantify_term(sig, data_ctor->type_variables);

		log(log_info, "found unchecked data ctor term for %s : %s",
				data_ctor->token.str().c_str(),
				sig_with_subs->str().c_str());
		return sig_with_subs;
	} else {
		log(log_warning, "not-impl: get a term from unchecked_var %s", node->str().c_str());
		not_impl();
		return types::term_unreachable();
	}
}

location unchecked_var_t::get_location() const {
	return node->token.location;
}
