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

unification_t::ref var_t::accepts_callsite(
		ptr<scope_t> scope,
	   	types::term::ref args) const
{
	types::term::ref sig = get_term();
	log(log_info, "checking whether %s accepts %s", str().c_str(),
		   	args->str().c_str());

	auto env = scope->get_type_env();

	status_t status;
	return unify(status, env, sig,
			types::term_product(PK_FUNCTION, {args, types::term_generic()}));
}
