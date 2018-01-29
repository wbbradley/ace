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
		types::type_t::ref type, 
		types::type_t::map &bindings) 
{
	/* make sure that if there are any free type variables, we mark
	 * them as unreachable */
	std::set<std::string> ftvs = type->get_ftvs();
	if (ftvs.size() != 0) {
		for (std::string ftv : ftvs) {
			if (!in(ftv, bindings)) {
				bindings[ftv] = type_unreachable();
			}
		}
	}
}

unification_t var_t::accepts_callsite(
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
	   	types::type_t::ref args,
		types::type_t::ref return_type) const
{
	/* get the args out of the sig */
	types::type_t::ref type = get_type(scope);
	types::type_function_t::ref fn_type = dyncast<const types::type_function_t>(type);

	INDENT(6, string_format(
				"checking whether %s at %s accepts %s and returns %s",
				str().c_str(),
				get_location().str().c_str(),
				args->str().c_str(),
				return_type->str().c_str()));

	auto bindings = scope->get_type_variable_bindings();

	auto u = unify(fn_type, type_function(args, return_type), scope->get_nominal_env());

	add_bindings_to_make_type_concrete(fn_type->args, u.bindings);
	add_bindings_to_make_type_concrete(fn_type->return_type, u.bindings);

	debug_above(6, log(log_info, "check of %s %s",
				str().c_str(),
				u.result ? c_good("succeeded") : c_error("failed")));
	return u;
}
