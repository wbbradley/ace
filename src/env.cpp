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

env_t env_t::rebind(const types::type_t::map &env) const {
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
	debug_above(9, log("extending env with %s => %s", id.str().c_str(), str(new_map).c_str()));
	return env_t{new_map, return_type_};
}

types::predicate_map env_t::get_predicate_map() const {
	types::predicate_map predicate_map;
	for (auto pair : map) {
		mutating_merge(pair.second->get_predicate_map(), predicate_map);
	}
	return predicate_map;
}

std::string str(const env_t::map_t &m) {
	std::stringstream ss;
	ss << "{";
	ss << join_with(m, ", ", [] (const auto &pair) {
			return string_format("%s: %s", pair.first.c_str(), pair.second->str().c_str());
			});
	ss << "}";
	return ss.str();
}
