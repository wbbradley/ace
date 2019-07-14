#pragma once
#include <unordered_set>

#include "ast_decls.h"
#include "data_ctors_map.h"
#include "defn_id.h"
#include "tracked_types.h"
#include "types.h"

namespace zion {

const ast::Expr *translate_match_expr(
    const types::DefnId &for_defn_id,
    const ast::Match *match,
    const DataCtorsMap &data_ctors_map,
    const std::unordered_set<std::string> &bound_vars,
    const TrackedTypes &tracked_types,
    const types::TypeEnv &type_env,
    TrackedTypes &typing,
    types::NeededDefns &needed_defns,
    bool &returns);

typedef const std::function<const ast::Expr *(
    const DataCtorsMap &data_ctors_map,
    const std::unordered_set<std::string> &bound_vars,
    const TrackedTypes &tracked_types,
    const types::TypeEnv &type_env,
    TrackedTypes &typing,
    types::NeededDefns &needed_defns,
    bool &returns)> &TranslateContinuationFn;

} // namespace zion
