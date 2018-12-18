#include "scope.h"
#include "user_error.h"

void scope_t::add_name(std::string name, location_t location) {
	if (in(name, map)) {
		auto error = user_error(location, "duplicate name " c_id("%s") " found", name.c_str());
		error.add_info(map[name], "see prior declaration here");
		throw error;
	}
	map[name] = location;
}

bool scope_t::exists(std::string name) const {
	return in(name, map);
}
