#include "defn_id.h"
#include "types.h"

location_t defn_id_t::get_location() const {
	return id.location;
}

std::string defn_id_t::str() const {
	return C_ID + repr() + C_RESET;
}

std::string defn_id_t::repr() const {
	assert(id.name[0] != '(');
	if (cached_repr.size() != 0) {
		return cached_repr;
	} else {
		cached_repr = id.name + c_type(" :: ") + scheme->str();
		return cached_repr;
	}
}

identifier_t defn_id_t::repr_id() const {
	return {repr(), id.location};
}

bool defn_id_t::operator <(const defn_id_t &rhs) const {
	return repr() < rhs.repr();
}

types::type_t::ref defn_id_t::get_lambda_param_type() const {
	auto lambda_type = safe_dyncast<const types::type_operator_t>(scheme->instantiate(INTERNAL_LOC()));
	return lambda_type->oper;
}

types::type_t::ref defn_id_t::get_lambda_return_type() const {
	auto lambda_type = safe_dyncast<const types::type_operator_t>(scheme->instantiate(INTERNAL_LOC()));
	return lambda_type->operand;
}

std::ostream &operator <<(std::ostream &os, const defn_id_t &defn_id) {
	return os << defn_id.str();
}
