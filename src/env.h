#pragma once

#include "ptr.h"
#include <string>

namespace types {
	struct type_t;
};

struct env_t {
	typedef const ptr<const env_t>& ref;
	virtual ~env_t() {}
	virtual ptr<const types::type_t> get_nominal_type(const std::string &name) const = 0;
	virtual ptr<const types::type_t> get_total_type(const std::string &name) const = 0;
};
