#include "zion.h"
#include "logger.h"
#include "utils.h"
#include "dbg.h"
#include "var.h"
#include <sstream>
#include "unification.h"
#include "scopes.h"
#include <iostream>

std::string str(const var_t::refs &vars) {
	std::stringstream ss;
	ss << "[" << join_str(vars, ", ") << "]";
	return ss.str();
}

void add_bindings_to_make_type_concrete(
		types::type::ref type, 
		types::type::map &bindings) 
{
	/* make sure that if there are any free type variables, we mark
	 * them as unreachable */
	atom::set ftvs = type->get_ftvs();
	if (ftvs.size() != 0) {
		for (atom ftv : ftvs) {
			if (!in(ftv, bindings)) {
				bindings[ftv] = type_unreachable();
			}
		}
	}
}

unification_t var_t::accepts_callsite(
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		types::type::ref type_fn_context,
	   	types::type_args::ref args) const
{
	/* get the args out of the sig */
	types::type::ref type = get_type(scope);
	types::type_function::ref fn_type = dyncast<const types::type_function>(type);
	auto env = scope->get_typename_env();

	indent_logger indent(6, string_format(
				"checking whether %s accepts %s", str().c_str(),
				args->str().c_str()));

	auto bindings = scope->get_type_variable_bindings();

	/* consider allowing the caller to invoke from a different context
	 * deliberately in order to claim access to a separate module's context */
	auto u = unify(
			fn_type,
		   	type_function(type_fn_context, args, type_variable(INTERNAL_LOC())),
		   	env);

	add_bindings_to_make_type_concrete(fn_type->args, u.bindings);
	add_bindings_to_make_type_concrete(fn_type->return_type, u.bindings);

	debug_above(6, log(log_info, "check of %s %s",
				str().c_str(),
				u.result ? c_good("succeeded") : c_error("failed")));
	return u;
}
