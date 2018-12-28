#include "env.h"
#include "types.h"
#include "user_error.h"
#include <iostream>

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

void env_t::rebind(const types::type_t::map &bindings) {
	if (bindings.size() == 0) {
		return;
	}
	for (auto pair : map) {
		map[pair.first] = pair.second->rebind(bindings);
	}
	std::vector<instance_requirement_t> new_instance_requirements;
	for (auto &ir : instance_requirements) {
		new_instance_requirements.push_back(instance_requirement_t{ir.type_class_name, ir.location, ir.type->rebind(bindings)});
	}
	std::swap(instance_requirements, new_instance_requirements);
}

void env_t::add_instance_requirement(const instance_requirement_t &ir) {
	debug_above(6,
		   	log_location(
				log_info,
				ir.location,
				"adding type class requirement for %s %s",
				ir.type_class_name.c_str(), ir.type->str().c_str()));
	instance_requirements.push_back(ir);
}

void env_t::extend(identifier_t id, types::scheme_t::ref scheme, bool allow_subscoping) {
	if (!allow_subscoping && in(id.name, map)) {
		throw user_error(id.location, "duplicate symbol (TODO: make this error better)");
	}
	map[id.name] = scheme;
	debug_above(9, log("extending env with %s => %s", id.str().c_str(), scheme->str().c_str()));
}

types::predicate_map env_t::get_predicate_map() const {
	types::predicate_map predicate_map;
	for (auto pair : map) {
		mutating_merge(pair.second->get_predicate_map(), predicate_map);
	}
	return predicate_map;
}

std::string str(const types::scheme_t::map &m) {
	std::stringstream ss;
	ss << "{";
	ss << join_with(m, ", ", [] (const auto &pair) {
			return string_format("%s: %s", pair.first.c_str(), pair.second->str().c_str());
			});
	ss << "}";
	return ss.str();
}

std::string env_t::str() const {
	std::stringstream ss;
	ss << "{context: " << ::str(map);
	if (return_type != nullptr) {
		ss << ", return_type: (" << return_type->str() << ")";
	}
	if (instance_requirements.size() != 0) {
		ss << ", instance_requirements: [" << join_with(instance_requirements, ", ", [] (const instance_requirement_t &ir) {
				std::stringstream ss;
				ss << "{" << ir.type_class_name << ", " << ir.location << ", " << ir.type->str() << "}";
				return ss.str();
				}) << "]";
	}
	ss << "}";
	return ss.str();
}

std::string instance_requirement_t::str() const {
	std::stringstream ss;
	ss << type_class_name << " " << type << " at " << location;
	return ss.str();
}
