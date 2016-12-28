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
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
	   	types::type::ref args) const
{
	/* get the args out of the sig */
	types::type::ref fn_type = get_type(scope);
	auto env = scope->get_typename_env();

	indent_logger indent(2, string_format(
				"checking whether %s accepts %s in env %s", str().c_str(),
				args->str().c_str(), ::str(env).c_str()));

	// scope->dump(std::cerr);
	// dbg();
	auto bindings = scope->get_type_variable_bindings();

	assert(bindings.size() == 0 && "Should we be using these bindings here?");
	auto u = unify(
			fn_type,
		   	type_product(pk_function, {args, type_variable()}),
		   	env);

	debug_above(2, log(log_info, "check of %s %s",
				str().c_str(),
				u.result ? "succeeded" : "failed"));
	return u;
}
