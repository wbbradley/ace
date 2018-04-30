#pragma once
#include "types.h"

bound_var_t::ref cast_data_type_to_ctor_struct(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		location_t value_location,
		bound_var_t::ref input_value,
		token_t ctor_name);

namespace patterns {
	struct Pattern;

	struct CtorPattern;
	struct CtorPatterns;
	struct AllOf;
	struct Nothing;

	struct Pattern {
		typedef ptr<const Pattern> ref;

		virtual ~Pattern() {}
		virtual ptr<const CtorPattern> asCtorPattern() const { return nullptr; }
		virtual ptr<const CtorPatterns> asCtorPatterns() const { return nullptr; }
		virtual ptr<const AllOf> asAllOf() const { return nullptr; }
		virtual ptr<const Nothing> asNothing() const { return nullptr; }
		virtual std::string str() const = 0;
	};

	struct CtorPatternValue {
		identifier::ref type_name;
		identifier::ref name;
		std::vector<Pattern::ref> args;
	};

	struct CtorPattern : std::enable_shared_from_this<CtorPattern>, Pattern {
		CtorPatternValue cpv;

		virtual ptr<const CtorPattern> asCtorPattern() const { return shared_from_this(); }
		virtual std::string str() const;
	};

	struct CtorPatterns : std::enable_shared_from_this<CtorPatterns>, Pattern {
		std::vector<CtorPatternValue> cpvs;

		virtual ptr<const CtorPatterns> asCtorPatterns() const { return shared_from_this(); }
		virtual std::string str() const;
	};

	struct AllOf : std::enable_shared_from_this<AllOf>, Pattern {
		types::type_t::ref type;

		AllOf(types::type_t::ref type) : type(type) {}
		virtual ptr<const AllOf> asAllOf() const { return shared_from_this(); }
		virtual std::string str() const;
	};

	struct Nothing : std::enable_shared_from_this<Nothing>, Pattern {
		virtual ptr<const Nothing> asNothing() const { return shared_from_this(); }
		virtual std::string str() const;
	};

	Pattern::ref pattern_intersect(Pattern::ref lhs, Pattern::ref rhs);
	Pattern::ref pattern_difference(Pattern::ref lhs, Pattern::ref rhs);
}

