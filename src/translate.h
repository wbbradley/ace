#pragma once
#include "ast.h"
#include "env.h"
#include "types.h"
#include "defn_id.h"
#include <list>
#include <unordered_set>

struct translation_t {
	typedef std::shared_ptr<translation_t> ref;
	translation_t(
			bitter::expr_t *expr,
			const std::unordered_map<bitter::expr_t *, types::type_t::ref> &typing) :
		expr(expr),
		typing(typing)
	{}

	bitter::expr_t * const expr;
	std::unordered_map<bitter::expr_t *, types::type_t::ref> const typing;

	std::string str() const;
	location_t get_location() const;
};

std::shared_ptr<translation_t> translate(
		bitter::expr_t *expr,
		const std::unordered_set<std::string> &bound_vars,
	   	const std::function<types::type_t::ref (bitter::expr_t *)> &get_type,
		std::list<defn_id_t> &needed_defns);

