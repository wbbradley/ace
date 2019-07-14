#pragma once
#include <list>
#include <unordered_set>

#include "ast_decls.h"
#include "defn_id.h"
#include "tracked_types.h"
#include "types.h"

namespace zion {

struct Translation {
  typedef std::shared_ptr<Translation> ref;

  Translation(const ast::Expr *expr, const TrackedTypes &typing);

  const ast::Expr *expr;
  TrackedTypes const typing;

  std::string str() const;
  Location get_location() const;
};

types::Map get_data_ctors_types(const DataCtorsMap &data_ctors_map,
                                types::Ref type);
types::Ref get_data_ctor_type(const DataCtorsMap &data_ctors_map,
                              types::Ref type,
                              Identifier ctor_id);
int get_ctor_id(const CtorIdMap &ctor_id_map, std::string ctor_name);

Translation::ref translate_expr(
    const types::DefnId &for_defn_id,
    const ast::Expr *expr,
    const DataCtorsMap &data_ctors_map,
    const std::unordered_set<std::string> &bound_vars,
    const TrackedTypes &tracked_types,
    const types::TypeEnv &type_env,
    types::NeededDefns &needed_defns,
    bool &returns);
const ast::Expr *texpr(const types::DefnId &for_defn_id,
                       const ast::Expr *expr,
                       const DataCtorsMap &data_ctors_map,
                       const std::unordered_set<std::string> &bound_vars,
                       const TrackedTypes &tracked_types,
                       types::Ref type,
                       const types::TypeEnv &type_env,
                       TrackedTypes &typing,
                       types::NeededDefns &needed_defns,
                       // TODO: pass in overloads in order to perform resolution
                       bool &returns);

} // namespace zion
