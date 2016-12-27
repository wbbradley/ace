#include "zion.h"
#include "dbg.h"
#include "unchecked_var.h"
#include "ast.h"
#include "bound_type.h"

std::string unchecked_var_t::str() const {
    std::stringstream ss;
    ss << id->str() << " : " << node->token.str() << " : unchecked";
    return ss.str();
}

types::type::ref unchecked_data_ctor_t::get_type(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	ptr<scope_t> scope) const
{
	return sig;
}

types::type::ref unchecked_var_t::get_type(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope) const
{
	if (auto fn = dyncast<const ast::function_defn>(node)) {
		auto decl = fn->decl;
		assert(decl != nullptr);

		if (decl->param_list_decl != nullptr) {
			/* this is a function declaration, so let's set up our output parameters */
			types::type::refs args;

			/* get the parameters */
			auto &params = decl->param_list_decl->params;
			for (auto &param : params) {
				if (!param->type_ref) {
					args.push_back(types::type_generic());
				} else {
                    assert(0);
#if 0
					args.push_back(param->type_ref->get_type_type(status,
								builder, scope, nullptr, {}));
#endif
				}
			}

			if (decl->return_type_ref) {
                assert(0);
#if 0
				auto return_type_type = decl->return_type_ref->get_type_term(
						status, builder, scope, nullptr, {});
#endif

				if (!!status) {
					/* get the return type */
					types::type::ref sig = get_function_type(
							get_args_type(args),
							return_type);

					debug_above(9, log(log_info, "found unchecked type for %s : %s",
								decl->token.str().c_str(),
								sig->str().c_str()));
					return sig;
				} else {
					log(log_warning, "unable to get type for return type");
					not_impl();
					return type_unreachable();
				}
			} else {
				types::type::ref sig = get_function_type(
						get_args_type(args),
						/* default to void, which is fully bound */
						type_unreachable());

				debug_above(4, log(log_info, "defaulting return type of %s to void : %s",
							decl->token.str().c_str(),
							sig->str().c_str()));
				return sig;
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
}

location unchecked_var_t::get_location() const {
	return node->token.location;
}
