#pragma once
#include "types.h"

bitter::expr_t *translate_match_expr(
		const defn_id_t &for_defn_id,
		bitter::match_t *match,
		const std::unordered_set<std::string> &bound_vars,
		const std::function<types::type_t::ref (bitter::expr_t *)> &get_type,
		std::unordered_map<bitter::expr_t *, types::type_t::ref> &typing,
		std::set<defn_id_t> &needed_defns);
