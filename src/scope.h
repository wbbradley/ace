#pragma once
#include <string>
#include <unordered_map>
#include "location.h"

struct scope_t {
	void add_name(std::string, location_t);
	bool exists(std::string) const;

	std::unordered_map<std::string, location_t> map;
};
