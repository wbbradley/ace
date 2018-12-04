#include "binding.h"
#include "utils.h"

std::string binding_t::str() const {
	return string_format("%s : %s : %s", name.c_str(), signature.c_str(), location.str().c_str());
}

bool operator <(const binding_t &lhs, const binding_t &rhs) {
	if (lhs.location < rhs.location) {
		return true;
	} else if (lhs.location == rhs.location) {
		if (lhs.name < rhs.name) {
			return true;
		} else if (lhs.name == rhs.name) {
			return lhs.signature < rhs.signature;
		} else {
			return false;
		}
	} else {
		return false;
	}
}
