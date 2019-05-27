#pragma once
#include <list>
#include <unordered_set>

#include "ast_decls.h"
#include "defn_id.h"
#include "types.h"

struct translation_t {
  typedef std::shared_ptr<translation_t> ref;

  translation_t(const bitter::expr_t *expr, const tracked_types_t &typing);

  const bitter::expr_t *const expr;
  tracked_types_t const typing;

  std::string str() const;
  location_t get_location() const;
};

struct translation_env_t {
  translation_env_t(std::shared_ptr<tracked_types_t> tracked_types,
                    const ctor_id_map_t &ctor_id_map,
                    const data_ctors_map_t &data_ctors_map)
      : tracked_types(tracked_types), ctor_id_map(ctor_id_map),
        data_ctors_map(data_ctors_map) {
  }

  std::shared_ptr<tracked_types_t> tracked_types;
  const ctor_id_map_t &ctor_id_map;
  const data_ctors_map_t &data_ctors_map;

  types::type_t::ref get_type(const bitter::expr_t *e) const;
  std::map<std::string, types::type_t::refs> get_data_ctors_terms(
      types::type_t::ref type) const;
  types::type_t::refs get_data_ctor_terms(types::type_t::ref type,
                                          identifier_t ctor_id) const;
  types::type_t::refs get_fresh_data_ctor_terms(identifier_t ctor_id) const;
  int get_ctor_id(std::string ctor_name) const;
};

translation_t::ref translate_expr(
    const defn_id_t &for_defn_id,
    bitter::expr_t *expr,
    const std::unordered_set<std::string> &bound_vars,
    const types::type_env_t &type_env,
    const translation_env_t &tenv,
    needed_defns_t &needed_defns,
    bool &returns);
bitter::expr_t *texpr(const defn_id_t &for_defn_id,
                      bitter::expr_t *expr,
                      const std::unordered_set<std::string> &bound_vars,
                      types::type_t::ref type,
                      const types::type_env_t &type_env,
                      const translation_env_t &tenv,
                      tracked_types_t &typing,
                      needed_defns_t &needed_defns,
                      bool &returns);
