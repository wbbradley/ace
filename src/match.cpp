#include "zion.h"
#include "match.h"
#include "ast.h"

namespace match {
	Pattern::ref pattern_intersect(Pattern::ref lhs, Pattern::ref rhs) {
		assert(false);
		return nullptr;
	}

	Pattern::ref pattern_difference(Pattern::ref lhs, Pattern::ref rhs) {
		assert(false);
		return nullptr;
	}

	std::string AllOf::str() const {
		std::stringstream ss;
		ss << "AllOf(" << type->str() << ")";
		return ss.str();
	}

	std::string Nothing::str() const {
		return "Nothing";
	}

	std::string CtorPattern::str() const {
		std::stringstream ss;
		ss << "CtorPattern(" << cpv.str() << ")";
		return ss.str();
	}

	std::string CtorPatterns::str() const {
		std::stringstream ss;
		ss << "CtorPatterns(" << ::join_with(cpvs, ", ", [](const CtorPatternValue &cpv) -> std::string { return cpv.str(); }) << ")";
		return ss.str();
	}

	std::string CtorPatternValue::str() const {
		std::stringstream ss;
		ss << "CtorPatternValue(" << type_name->str() << ", " << name->str() << ", ";
		ss << ::join_str(args, ", ") << ")";
		return ss.str();
	}

}

namespace ast {
	using namespace ::match;

	Pattern::ref ctor_predicate_t::get_pattern() const {
		assert(false);
		return nullptr;
	}
	Pattern::ref irrefutable_predicate_t::get_pattern() const {
		assert(false);
		return nullptr;
	}
	Pattern::ref literal_expr_t::get_pattern() const {
		assert(false);
		return nullptr;
	}
}
