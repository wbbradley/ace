#include "types.h"

struct parse_state_t;

namespace types {
	type_t::ref parse_type(parse_state_t &ps, const std::set<identifier_t> &generics);
	type_t::ref parse_function_type(parse_state_t &ps, location_t location, std::set<identifier_t> generics, identifier_t &name, types::type_t::ref default_return_type);
	type_args_t::ref parse_data_ctor_type(parse_state_t &ps, const std::set<identifier_t> &generics);
	type_t::ref parse_product_type(parse_state_t &ps, const std::set<identifier_t> &generics);
	identifier_t reduce_ids(const std::list<identifier_t> &ids, location_t location);
}

bool token_is_illegal_in_type(const token_t &token);
types::type_t::ref parse_type_expr(std::string input, std::set<identifier_t> generics, identifier_t module_id);
