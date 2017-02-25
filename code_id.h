#pragma once
#include "token.h"
#include "identifier.h"

struct code_id : public identifier {
	zion_token_t token;

	code_id(const zion_token_t token) : token(token) {
	}

	code_id() = delete;
	code_id(const code_id &) = delete;
	code_id(const code_id &&) = delete;
	code_id &operator =(const code_id &) = delete;

	virtual atom get_name() const {
		return {token.text};
	}

	virtual location_t get_location() const {
		return token.location;
	}

	virtual std::string str() const {
		return token.str();
	}
};

struct type_id_code_id : public identifier {
	type_id_code_id(const location_t location, atom var_name) :
		location(location), name(string_format("typeid(%s)", var_name.c_str()))
	{
	}

	virtual atom get_name() const {
		return name;
	}

	virtual location_t get_location() const {
		return location;
	}

	virtual std::string str() const {
		return name.str();
	}

	const location_t location;
	const atom name;
};

identifier::ref make_code_id(const zion_token_t &token);
identifier::ref make_type_id_code_id(const location_t location, atom var_name);
