#include "env.h"
#include "types.h"
#include "user_error.h"

types::type_t::ref env_t::maybe_lookup_env(identifier_t id) const {
	auto iter = map.find(id.name);
	if (iter != map.end()) {
		return iter->second->instantiate(id.location);
	} else {
		return nullptr;
	}
}

types::type_t::ref env_t::lookup_env(identifier_t id) const {
	auto type = maybe_lookup_env(id);
	if (type != nullptr) {
		return type;
	}
	throw user_error(id.location, "unbound variable " C_ID "%s" C_RESET, id.name.c_str());
}

env_t env_t::rebind(const types::type_t::map &env) {
	if (env.size() == 0) {
		return *this;
	}
	env_t new_env;
	for (auto pair : map) {
		new_env.map[pair.first] = pair.second->rebind(env);
	}
	return new_env;
}

env_t env_t::extend(identifier_t id, types::forall_t::ref scheme) const {
	return extend(id, return_type, scheme);
}

env_t env_t::extend(identifier_t id, types::type_t::ref return_type_, types::forall_t::ref scheme) const {
	map_t new_map{map};
	new_map[id.name] = scheme;
	return env_t{new_map, return_type_};
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
