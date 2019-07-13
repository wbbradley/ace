#pragma once
#include <unordered_set>

#include "defn_id.h"
#include "types.h"

namespace zion {

struct TranslationEnv;

const ast::Expr *translate_match_expr(
    const types::DefnId &for_defn_id,
    const ast::Match *match,
    const std::unordered_set<std::string> &bound_vars,
    const types::TypeEnv &type_env,
    const TranslationEnv &tenv,
    TrackedTypes &typing,
    types::NeededDefns &needed_defns,
    bool &returns);

typedef const std::function<const ast::Expr *(
    const std::unordered_set<std::string> &bound_vars,
    const types::TypeEnv &type_env,
    const TranslationEnv &tenv,
    TrackedTypes &typing,
    types::NeededDefns &needed_defns,
    bool &returns)> &translate_continuation_t;

} // namespace zion
