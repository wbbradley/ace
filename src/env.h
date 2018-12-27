#pragma once
#include <string>
#include <set>
#include "identifier.h"
#include "types.h"

namespace types {
	struct type_t;
	struct scheme_t;
};

struct instance_requirement_t {
	std::string type_class_name;
	location_t location;
	types::type_t::ref type;
};

struct env_t {
	using ref = const env_t &;

	types::scheme_t::map map;
	std::shared_ptr<const types::type_t> return_type;
	std::vector<instance_requirement_t> instance_requirements;

	void add_instance_requirement(const instance_requirement_t &ir);
	void extend(identifier_t id, std::shared_ptr<types::scheme_t> scheme, bool allow_subscoping);
	void rebind(const types::type_t::map &env);
	types::predicate_map get_predicate_map() const;
	std::shared_ptr<const types::type_t> lookup_env(identifier_t id) const;
	std::shared_ptr<const types::type_t> maybe_lookup_env(identifier_t id) const;
	std::string str() const;
};

std::string str(const types::scheme_t::map &m);
