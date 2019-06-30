#pragma once
#include <unordered_set>

#include "defn_id.h"
#include "types.h"

struct TranslationEnv;

bitter::Expr *translate_match_expr(
    const DefnId &for_defn_id,
    bitter::Match *match,
    const std::unordered_set<std::string> &bound_vars,
    const types::TypeEnv &type_env,
    const TranslationEnv &tenv,
    TrackedTypes &typing,
    NeededDefns &needed_defns,
    bool &returns);

typedef const std::function<bitter::Expr *(
    const std::unordered_set<std::string> &bound_vars,
    const types::TypeEnv &type_env,
    const TranslationEnv &tenv,
    TrackedTypes &typing,
    NeededDefns &needed_defns,
    bool &returns)> &translate_continuation_t;
