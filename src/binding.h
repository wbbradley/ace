#pragma once
#include "location.h"
#include <string>
#include <set>

struct binding_t {
	location_t location;
	std::string name;
	std::string signature;

	std::string str() const;
};

extern bool operator <(const binding_t &lhs, const binding_t &rhs);

struct bindings_less_than_t : public std::binary_function<binding_t, binding_t, bool>
{
	bool operator()(const binding_t& lhs, const binding_t& rhs) const
	{
		return lhs < rhs;
	}
};

typedef std::set<binding_t, bindings_less_than_t> bindings_set_t;
