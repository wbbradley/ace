#pragma once
#include "identifier.h"
#include "types.h"
#include <string>

struct defn_id_t {
	identifier_t const id;
	types::scheme_t::ref const scheme;
	mutable std::string cached_str;

	location_t get_location() const;
	std::string str() const;
	bool operator <(const defn_id_t &rhs) const;
};
