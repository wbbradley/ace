#include "env.h"
#include "types.h"
#include "user_error.h"

types::type_t::ref env_t::lookup_env(identifier_t id) const {
	auto iter = map.find(id.name);
	if (iter != map.end()) {
		return iter->second->instantiate(id.location);
	} else {
		throw user_error(id.location, "unbound variable " C_ID "%s" C_RESET, id.name.c_str());
	}
}

env_t env_t::extend(identifier_t id, types::forall_t::ref scheme) const {
	map_t new_map{map};
	new_map[id.name] = scheme;
	return env_t{new_map};
}

std::set<std::string> env_t::get_ftvs() const {
	std::set<std::string> ftvs;
	for (auto pair : map) {
		for (auto v : pair.second->get_ftvs()) {
			ftvs.insert(v);
		}
	}
	return ftvs;
}
