#include "typed_id.h"

std::string typed_id_t::repr() const {
	assert(id.name[0] != '(');
	if (cached_repr.size() != 0) {
		return cached_repr;
	} else {
		cached_repr = "\"" + id.name + " :: " + type->repr() + "\"";
		return cached_repr;
	}
}

std::ostream &operator <<(std::ostream &os, const typed_id_t &typed_id) {
	return os << typed_id.repr();
}

bool typed_id_t::operator <(const typed_id_t &rhs) const {
	return repr() < rhs.repr();
}

