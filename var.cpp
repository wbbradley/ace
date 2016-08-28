#include "zion.h"
#include "utils.h"
#include "dbg.h"
#include "var.h"
#include <sstream>
#include "unification.h"
#include "scopes.h"

std::string str(const var_t::refs &vars) {
	std::stringstream ss;
	ss << "[" << join_str(vars, ", ") << "]";
	return ss.str();
}

unification_t var_t::accepts_callsite(
		ptr<scope_t> scope,
	   	types::term::ref args) const
{
	/* get the args out of the sig */
	types::term::ref fn_args = get_function_term_args(get_term());
	auto env = scope->get_type_env();

	debug_above(2, log(log_info, "checking whether %s accepts %s in env %s",
			   	fn_args->str().c_str(),
				args->str().c_str(),
				::str(env).c_str()));

	return unify(fn_args, args, env);
}
