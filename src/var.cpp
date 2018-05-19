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

unification_t var_t::accepts_callsite(
		ptr<scope_t> scope,
	   	types::type_t::ref args,
		types::type_t::ref return_type) const
{
	/* get the args out of the sig */
	types::type_t::ref type = get_type(scope)->eval(scope);
	debug_above(9, log("var_t type = %s", str().c_str()));

	types::type_function_t::ref fn_type;
	if (auto function_closure = dyncast<const types::type_function_closure_t>(type)) {
		fn_type = dyncast<const types::type_function_t>(function_closure->function);
	} else {
		fn_type = dyncast<const types::type_function_t>(type);
	}
	assert(fn_type != nullptr);

	INDENT(6, string_format(
				"checking whether %s at %s accepts %s and returns %s",
				str().c_str(),
				get_location().str().c_str(),
				args->str().c_str(),
				return_type->str().c_str()));

	auto bindings = scope->get_type_variable_bindings();

	auto u = unify(fn_type, type_function(INTERNAL_LOC(), nullptr, args, return_type), scope, bindings);

	debug_above(6, log(log_info, "check of %s %s",
				str().c_str(),
				u.result ? c_good("succeeded") : c_error("failed")));
	return u;
}
