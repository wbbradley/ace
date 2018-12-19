#pragma once
#include "types.h"
#include <set>

namespace match {
	struct Pattern;
	struct Nothing;

	struct Pattern {
		typedef std::shared_ptr<const Pattern> ref;

		location_t location;

		Pattern(location_t location) : location(location) {}
		virtual ~Pattern() {}

		virtual std::shared_ptr<const Nothing> asNothing() const { return nullptr; }
		virtual std::string str() const = 0;
	};

	extern std::shared_ptr<Nothing> theNothing;
	Pattern::ref intersect(Pattern::ref lhs, Pattern::ref rhs);
	Pattern::ref difference(Pattern::ref lhs, Pattern::ref rhs);
	Pattern::ref pattern_union(Pattern::ref lhs, Pattern::ref rhs);
	Pattern::ref all_of(location_t location, maybe<identifier_t> expr, env_t::ref env, types::type_t::ref type);
}

