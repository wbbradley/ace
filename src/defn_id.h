#pragma once
#include "identifier.h"
#include <string>

namespace types {
	struct scheme_t;
}

struct defn_id_t {
	identifier_t const id;
	std::shared_ptr<types::scheme_t> const scheme;
	mutable std::string cached_str;

	location_t get_location() const;
	std::string str() const;
	bool operator <(const defn_id_t &rhs) const;
};
