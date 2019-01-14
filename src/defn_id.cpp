#include "defn_id.h"
#include "types.h"

location_t defn_id_t::get_location() const {
	return id.location;
}

std::string defn_id_t::str() const {
	if (cached_str.size() != 0) {
		return cached_str;
	} else {
		cached_str = "(" + id.name + " :: " + scheme->str() + ")";
		return cached_str;
	}
}
bool defn_id_t::operator <(const defn_id_t &rhs) const {
	return str() < rhs.str();
}
