#pragma once
#include <string>
#include <set>
#include "identifier.h"

namespace types {
	struct type_t;
	struct forall_t;
};

struct env_t {
	using ref = const env_t &;
	using map_t = std::map<std::string, std::shared_ptr<types::forall_t>>;

	map_t map;

	env_t extend(identifier_t id, std::shared_ptr<types::forall_t> scheme) const;
	std::set<std::string> get_ftvs() const;
	std::shared_ptr<const types::type_t> lookup_env(identifier_t id) const;
};
