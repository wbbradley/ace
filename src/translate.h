#pragma once
#include <list>
#include <unordered_set>

#include "ast_decls.h"
#include "defn_id.h"
#include "types.h"

struct Translation {
  typedef std::shared_ptr<Translation> ref;

  Translation(const bitter::Expr *expr, const tracked_types_t &typing);

  const bitter::Expr *const expr;
  tracked_types_t const typing;

  std::string str() const;
  Location get_location() const;
};

struct TranslationEnv {
  TranslationEnv(std::shared_ptr<tracked_types_t> tracked_types,
                 const ctor_id_map_t &ctor_id_map,
                 const data_ctors_map_t &data_ctors_map)
      : tracked_types(tracked_types), ctor_id_map(ctor_id_map),
        data_ctors_map(data_ctors_map) {
  }

  std::shared_ptr<tracked_types_t> tracked_types;
  const ctor_id_map_t &ctor_id_map;
  const data_ctors_map_t &data_ctors_map;

  types::Type::ref get_type(const bitter::Expr *e) const;
  std::map<std::string, types::Type::refs> get_data_ctors_terms(
      types::Type::ref type) const;
  types::Type::refs get_data_ctor_terms(types::Type::ref type,
                                        Identifier ctor_id) const;
  types::Type::refs get_fresh_data_ctor_terms(Identifier ctor_id) const;
  int get_ctor_id(std::string ctor_name) const;
};

Translation::ref translate_expr(
    const DefnId &for_defn_id,
    bitter::Expr *expr,
    const std::unordered_set<std::string> &bound_vars,
    const types::type_env_t &type_env,
    const TranslationEnv &tenv,
    needed_defns_t &needed_defns,
    bool &returns);
bitter::Expr *texpr(const DefnId &for_defn_id,
                    bitter::Expr *expr,
                    const std::unordered_set<std::string> &bound_vars,
                    types::Type::ref type,
                    const types::type_env_t &type_env,
                    const TranslationEnv &tenv,
                    tracked_types_t &typing,
                    needed_defns_t &needed_defns,
                    bool &returns);
