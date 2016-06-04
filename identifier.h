#pragma once
#include <string>
#include "location.h"
#include "token.h"

struct identifier_t {
	identifier_t(std::string id, const location &location={{""},-1,-1}) : id(id), location(location) {}
	identifier_t(atom id, const location &location={{""},-1,-1}) : id(id), location(location) {}
	identifier_t(const zion_token_t &token) : id(token.text), location(token.location) {}

	atom id;
	location location;
	std::string str() const;

	bool operator ==(const identifier_t &rhs) const {
		return id == rhs.id;
	}

	bool operator !=(const identifier_t &rhs) const {
		return id != rhs.id;
	}
};


