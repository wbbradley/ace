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

	virtual location get_location() const {
		return token.location;
	}

	virtual std::string str() const {
		return token.str();
	}
};

identifier::ref make_code_id(const zion_token_t &token);
