#pragma once
#include <list>
#include <unordered_set>

#include "ast_decls.h"
#include "defn_id.h"
#include "types.h"

struct Translation {
  typedef std::shared_ptr<Translation> ref;

  Translation(const bitter::Expr *expr, const TrackedTypes &typing);

  const bitter::Expr *expr;
  TrackedTypes const typing;

  std::string str() const;
  Location get_location() const;
};

struct TranslationEnv {
  TranslationEnv(std::shared_ptr<TrackedTypes> tracked_types,
                 const CtorIdMap &ctor_id_map,
                 const DataCtorsMap &data_ctors_map)
      : tracked_types(tracked_types), ctor_id_map(ctor_id_map),
        data_ctors_map(data_ctors_map) {
  }

  std::shared_ptr<TrackedTypes> tracked_types;
  const CtorIdMap &ctor_id_map;
  const DataCtorsMap &data_ctors_map;

  types::Ref get_type(const bitter::Expr *e) const;
  std::map<std::string, types::Ref> get_data_ctors_types(types::Ref type) const;
  types::Ref get_data_ctor_type(types::Ref type, Identifier ctor_id) const;
  types::Ref get_fresh_data_ctor_type(Identifier ctor_id) const;
  int get_ctor_id(std::string ctor_name) const;
};

Translation::ref translate_expr(
    const DefnId &for_defn_id,
    const bitter::Expr *expr,
    const std::unordered_set<std::string> &bound_vars,
    const types::TypeEnv &type_env,
    const TranslationEnv &tenv,
    NeededDefns &needed_defns,
    bool &returns);
const bitter::Expr *texpr(const DefnId &for_defn_id,
                          const bitter::Expr *expr,
                          const std::unordered_set<std::string> &bound_vars,
                          types::Ref type,
                          const types::TypeEnv &type_env,
                          const TranslationEnv &tenv,
                          TrackedTypes &typing,
                          NeededDefns &needed_defns,
                          bool &returns);
