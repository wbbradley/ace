#pragma once
#include <string>
#include <set>
#include "identifier.h"
#include "types.h"

namespace types {
	struct type_t;
	struct forall_t;
};

struct env_t {
	using ref = const env_t &;
	using map_t = std::map<std::string, std::shared_ptr<types::forall_t>>;

	map_t map;
	std::shared_ptr<const types::type_t> return_type;

	env_t extend(identifier_t id, std::shared_ptr<types::forall_t> scheme) const;
	env_t extend(identifier_t id, std::shared_ptr<const types::type_t> return_type, std::shared_ptr<types::forall_t> scheme) const;
	env_t rebind(const types::type_t::map &env) const;
	types::predicate_map get_predicate_map() const;
	std::shared_ptr<const types::type_t> lookup_env(identifier_t id) const;
	std::shared_ptr<const types::type_t> maybe_lookup_env(identifier_t id) const;
	std::string str() const;
};

std::string str(const env_t::map_t &m);
