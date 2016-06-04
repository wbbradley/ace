#pragma once
#include "types.h"

struct parsed_id_t : public types::identifier {
	zion_token_t token;

	parsed_id_t(const zion_token_t token) : token(token) {}
	parsed_id_t() = delete;
	parsed_id_t(const parsed_id_t &) = delete;
	parsed_id_t(const parsed_id_t &&) = delete;
	parsed_id_t &operator =(const parsed_id_t &) = delete;

	virtual atom get_name() const {
		return {token.text};
	}
};
