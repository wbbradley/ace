#pragma once

#include "ptr.h"
#include <string>

namespace types {
	struct type_t;
};

struct env_t {
	typedef const ptr<const env_t> ref;
	virtual ~env_t() {}
	virtual ptr<const types::type_t> get_type(const std::string &name, bool allow_structural_types=false) const = 0;
};
