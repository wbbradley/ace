#pragma once
#include "types.h"
#include "defn_id.h"
#include <unordered_set>

struct translation_env_t;

bitter::expr_t *translate_match_expr(
		const defn_id_t &for_defn_id,
		bitter::match_t *match,
		const std::unordered_set<std::string> &bound_vars,
		const translation_env_t &tenv,
		tracked_types_t &typing,
		std::set<defn_id_t> &needed_defns,
		bool &returns);

typedef const std::function<bitter::expr_t *(
		const std::unordered_set<std::string> &bound_vars,
		const translation_env_t &tenv,
		tracked_types_t &typing,
		std::set<defn_id_t> &needed_defns,
		bool &returns)> &translate_continuation_t;
