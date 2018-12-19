#pragma once
#include <vector>
#include "types.h"
#include "ast_decls.h"

using constraints_t = std::vector<std::pair<types::type_t::ref, types::type_t::ref>>;
types::type_t::ref infer(
		bitter::expr_t *expr,
		env_t::ref env,
	   	constraints_t &constraints);
