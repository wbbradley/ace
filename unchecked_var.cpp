#include "zion.h"
#include "dbg.h"
#include "unchecked_var.h"
#include "ast.h"
#include "bound_type.h"

std::string unchecked_var_t::str() const {
    std::stringstream ss;
    ss << id->str() << " : unchecked var";
    return ss.str();
}

std::string unchecked_data_ctor_t::str() const {
    std::stringstream ss;
    ss << C_ID << id->str() << C_RESET << " : unchecked data ctor : ";
	ss << sig->str();
    return ss.str();
}

types::type_function::ref unchecked_data_ctor_t::get_type(scope_t::ref scope) const {
	return sig;
}

types::type::ref unchecked_var_t::get_type(scope_t::ref scope) const {
	/* TODO: plumb status down here */
	status_t status;
	if (auto fn = dyncast<const ast::function_defn>(node)) {
		auto decl = fn->decl;
		assert(decl != nullptr);

		if (decl->param_list_decl != nullptr) {
			/* this is a function declaration, so let's set up our output parameters */
			types::type::refs args;

			/* get the parameters */
			auto &params = decl->param_list_decl->params;
			for (auto &param : params) {
				if (param->type == nullptr) {
					args.push_back(type_variable(param->get_location()));
				} else {
					args.push_back(param->type);
				}
			}

			if (!!status) {
				/* figure out the context of this declaration */
				types::type::ref inbound_context = decl->inbound_context;

				if (inbound_context == nullptr) {
					/* the function didn't specify an inbound context */
					inbound_context = module_scope->get_inbound_context();
				}

				/* figure out the return type */
				if (decl->return_type != nullptr) {
					/* get the return type */
					types::type_function::ref sig = get_function_type(
							inbound_context,
							get_args_type(args),
							decl->return_type);

					debug_above(9, log(log_info, "found unchecked type for %s : %s",
								decl->token.str().c_str(),
								sig->str().c_str()));
					return sig;
				} else {
					types::type_function::ref sig = get_function_type(
							inbound_context,
							get_args_type(args),
							/* default to void, which is fully bound */
							type_void());

					debug_above(4, log(log_info, "defaulting return type of %s to void : %s",
								decl->token.str().c_str(),
								sig->str().c_str()));
					return sig;
				}
			}
		} else {
			panic("function declaration has no parameter list");
			return type_unreachable();
		}
	} else {
		log(log_warning, "not-impl: get a type from unchecked_var %s", node->str().c_str());
		not_impl();
		return type_unreachable();
	}

	panic("dead end codepath.");
	assert(!status);
	return nullptr;
}

location unchecked_var_t::get_location() const {
	return node->token.location;
}
