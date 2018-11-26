#pragma once

struct binding_t {
	location_t location;
	std::string name;
	std::string signature;

	bool operator <(const binding_t &rhs) const {
		if (location < rhs.location) {
			return true;
		} else if (location == rhs.location) {
			if (name < rhs.name) {
				return true;
			} else if (name == rhs.name) {
				return signature < rhs.signature;
			} else {
				return false;
			}
		} else {
			return false;
		}
	}
};
