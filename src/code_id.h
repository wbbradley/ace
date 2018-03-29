#pragma once
#include "token.h"
#include "identifier.h"

struct code_id : public identifier {
	token_t token;

	code_id(const token_t token) : token(token) {
	}

	code_id() = delete;
	code_id(const code_id &) = delete;
	code_id(const code_id &&) = delete;
	code_id &operator =(const code_id &) = delete;

	virtual std::string get_name() const {
		return {token.text};
	}

	virtual location_t get_location() const {
		return token.location;
	}

	virtual std::string str() const {
		return token.str();
	}

	virtual token_t get_token() const {
		return token;
	}
};

struct type_id_code_id : public identifier {
	type_id_code_id(const location_t location, std::string var_name) :
		location(location), name(string_format("typeid(%s)", var_name.c_str()))
	{
	}

	virtual std::string get_name() const {
		return name;
	}

	virtual location_t get_location() const {
		return location;
	}

	virtual std::string str() const {
		return name;
	}

	virtual token_t get_token() const {
		return token_t(location, tk_identifier, name);
	}

	const location_t location;
	const std::string name;
};

identifier::ref make_code_id(const token_t &token);
identifier::ref make_type_id_code_id(const location_t location, std::string var_name);
