#pragma once
#include "identifier.h"
#include <string>

namespace types {
	struct scheme_t;
	struct type_t;
}

struct defn_id_t {
	defn_id_t(
			identifier_t const id,
			std::shared_ptr<types::scheme_t> const scheme) :
		id(id),
		scheme(scheme)
	{}

	identifier_t const id;
	std::shared_ptr<types::scheme_t> const scheme;

private:
	mutable std::string cached_repr;

public:
	location_t get_location() const;
	std::string repr() const;
	std::string str() const;
	bool operator <(const defn_id_t &rhs) const;
	std::shared_ptr<const types::type_t> get_lambda_param_type() const;
	std::shared_ptr<const types::type_t> get_lambda_return_type() const;
};
