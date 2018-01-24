#include "types.h"

struct parse_state_t;

namespace types {
	type_t::ref parse_type(parse_state_t &ps, const identifier::set &generics);
}

types::type_t::ref parse_type_expr(std::string input, identifier::set generics, identifier::ref module_id);
