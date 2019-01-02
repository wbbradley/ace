#pragma once
#include "identifier.h"
#include "types.h"
#include <string>

struct defn_id_t {
	identifier_t const id;
	types::scheme_t::ref const scheme;
	mutable std::string cached_str;

	location_t get_location() const {
		return id.location;
	}

	std::string str() const {
		if (cached_str.size() != 0) {
			return cached_str;
		} else {
			cached_str = "{" + id.name + "::" + scheme->str() + "}";
			return cached_str;
		}
	}
	bool operator <(const defn_id_t &rhs) const {
		return str() < rhs.str();
	}
};

