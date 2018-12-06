#pragma once

#include "ptr.h"
#include <string>

namespace types {
	struct type_t;
};

namespace ast {
	struct expression_t;
}

struct env_t {
	typedef const ptr<env_t> ref;
	virtual ~env_t() {}
	virtual ptr<const types::type_t> get_type(const std::string &name, bool allow_structural_types=false) const = 0;
	virtual ptr<const types::type_t> resolve_type(ptr<const ast::expression_t> expr, ptr<const types::type_t> expected_type) = 0;
};
