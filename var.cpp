#include "zion.h"
#include "logger.h"
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
	types::term::ref fn_term = get_term();
	auto env = scope->get_type_env();

	indent_logger indent(2, string_format(
				"checking whether %s accepts %s in env %s", str().c_str(),
				args->str().c_str(), ::str(env).c_str()));

	auto u = unify(
			fn_term,
		   	types::term_product(pk_function, {args, types::term_generic()}),
		   	env);

	debug_above(2, log(log_info, "check of %s %s",
				str().c_str(),
				u.result ? "succeeded" : "failed"));
	return u;
}
