#pragma once
#include <unordered_set>

#include "defn_id.h"
#include "types.h"

struct translation_env_t;

bitter::expr_t *translate_match_expr(const defn_id_t &for_defn_id,
                                     bitter::match_t *match,
                                     const std::unordered_set<std::string> &bound_vars,
                                     const translation_env_t &tenv,
                                     tracked_types_t &typing,
                                     needed_defns_t &needed_defns,
                                     bool &returns);

typedef const std::function<bitter::expr_t *(
    const std::unordered_set<std::string> &bound_vars,
    const translation_env_t &tenv,
    tracked_types_t &typing,
    needed_defns_t &needed_defns,
    bool &returns)> &translate_continuation_t;
