#pragma once

#include "ptr.h"
#include <string>

struct delegate_t;

namespace types {
	struct type_t;
};

namespace ast {
	struct expression_t;
}

struct env_t {
	typedef const std::shared_ptr<env_t> ref;
	virtual ~env_t() {}
	virtual std::shared_ptr<const types::type_t> get_type(const std::string &name, bool allow_structural_types=false) const = 0;
};
