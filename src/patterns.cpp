#include "patterns.h"

#include <iostream>

#include "ast.h"
#include "compiler.h"
#include "translate.h"
#include "zion.h"

using namespace bitter;

expr_t *build_patterns(const defn_id_t &for_defn_id,
                       const pattern_blocks_t &pattern_blocks,
                       int index,
                       const std::unordered_set<std::string> &bound_vars_,
                       const types::type_env_t &type_env,
                       const translation_env_t &tenv,
                       tracked_types_t &typing,
                       needed_defns_t &needed_defns,
                       bool &returns,
                       identifier_t scrutinee_id,
                       types::type_t::ref scrutinee_type,
                       types::type_t::ref expected_type) {
  if (index == pattern_blocks.size()) {
    auto last_block = unit_expr(INTERNAL_LOC());
    typing[last_block] = type_unit(INTERNAL_LOC());
    return last_block;
  } else {
    auto &pattern_block = pattern_blocks[index];

    /* if pattern-matches then let names = {names} in block else build next
     * pattern */
    auto scrutinee_id_with_name_assignment =
        pattern_block->predicate->instantiate_name_assignment();

    auto bound_vars = bound_vars_;
    bound_vars.insert(scrutinee_id_with_name_assignment.name);

    /* because we have coverage analysis for the patterns, we know we can
     * sometimes skip the checks, and just do the destructuring. */
    bool do_checks = (index != pattern_blocks.size() - 1);

    auto scrutinee = new var_t(scrutinee_id);
    typing[scrutinee] = scrutinee_type;
    debug_above(5, log("scrutinee_type of %s is %s", scrutinee->str().c_str(),
                       scrutinee_type->str().c_str()));
    auto expr = new let_t(
        scrutinee_id_with_name_assignment, scrutinee,
        pattern_block->predicate->translate(
            for_defn_id, scrutinee_id_with_name_assignment, scrutinee_type,
            do_checks, bound_vars, type_env, tenv, typing, needed_defns,
            returns,
            [&for_defn_id, &pattern_block](
                const std::unordered_set<std::string> &bound_vars,
                const types::type_env_t &type_env,
                const translation_env_t &tenv, tracked_types_t &typing,
                needed_defns_t &needed_defns, bool &returns) -> expr_t * {
              return texpr(for_defn_id, pattern_block->result, bound_vars,
                           type_env, tenv, typing, needed_defns, returns);
            },
            [index, &pattern_blocks, &for_defn_id, &scrutinee_id,
             &scrutinee_type, &expected_type](
                const std::unordered_set<std::string> &bound_vars,
                const types::type_env_t &type_env,
                const translation_env_t &tenv, tracked_types_t &typing,
                needed_defns_t &needed_defns, bool &returns) -> expr_t * {
              if (index + 1 < pattern_blocks.size()) {
                return build_patterns(for_defn_id, pattern_blocks, index + 1,
                                      bound_vars, type_env, tenv, typing,
                                      needed_defns, returns, scrutinee_id,
                                      scrutinee_type, expected_type);
              } else {
                assert(false);
                return nullptr;
              }
            }));

    typing[expr] = expected_type;
    return expr;
  }
}

void check_patterns(location_t location,
                    std::string expr,
                    const translation_env_t &tenv,
                    const pattern_blocks_t &pattern_blocks,
                    types::type_t::ref pattern_value_type) {
  match::Pattern::ref uncovered = match::all_of(
      location, maybe<identifier_t>(make_iid(expr)), tenv, pattern_value_type);
  for (auto pattern_block : pattern_blocks) {
    match::Pattern::ref covering = pattern_block->predicate->get_pattern(
        pattern_value_type, tenv);
    if (match::intersect(uncovered, covering)->asNothing() != nullptr) {
      auto error = user_error(pattern_block->predicate->get_location(),
                              "this pattern is already covered");
      if (uncovered->asNothing() != nullptr) {
        error.add_info(pattern_block->predicate->get_location(),
                       "there is nothing left to match by this point");
      } else {
        error.add_info(pattern_block->predicate->get_location(),
                       "so far you haven't covered: %s",
                       uncovered->str().c_str());
      }
      throw error;
    }

    debug_above(9, log("uncovered = %s", uncovered->str().c_str()));
    debug_above(9, log("covering = %s", covering->str().c_str()));
    uncovered = match::difference(uncovered, covering);
  }

  if (uncovered->asNothing() == nullptr) {
    auto error = user_error(location, "not all patterns are covered");
    error.add_info(location, "uncovered patterns: %s",
                   uncovered->str().c_str());
    throw error;
  }
}

expr_t *translate_match_expr(const defn_id_t &for_defn_id,
                             bitter::match_t *match,
                             const std::unordered_set<std::string> &bound_vars,
                             const types::type_env_t &type_env,
                             const translation_env_t &tenv,
                             tracked_types_t &typing,
                             needed_defns_t &needed_defns,
                             bool &returns) {
  auto expected_type = tenv.get_type(match);

  debug_above(6, log("match expression is expecting type %s",
                     expected_type->str().c_str()));

  auto scrutinee_expr = texpr(for_defn_id, match->scrutinee, bound_vars,
                              type_env, tenv, typing, needed_defns, returns);

  if (returns) {
    throw user_error(scrutinee_expr->get_location(),
                     "this value will return so the match seems pointless?");
  }

  auto scrutinee_type = tenv.get_type(match->scrutinee);

  check_patterns(scrutinee_expr->get_location(), match->scrutinee->str(), tenv,
                 match->pattern_blocks, scrutinee_type);

  identifier_t scrutinee_id = make_iid("__scrutinee_" + fresh());
  auto new_match = new let_t(
      scrutinee_id, scrutinee_expr,
      build_patterns(for_defn_id, match->pattern_blocks, 0, bound_vars,
                     type_env, tenv, typing, needed_defns, returns,
                     scrutinee_id, typing[scrutinee_expr], expected_type));
  typing[new_match] = expected_type;
  return new_match;
}

void literal_t::get_bound_vars(
    std::unordered_set<std::string> &bound_vars) const {
}

expr_t *literal_t::translate(const defn_id_t &for_defn_id,
                             const identifier_t &scrutinee_id,
                             const types::type_t::ref &scrutinee_type,
                             bool do_checks,
                             const std::unordered_set<std::string> &bound_vars,
                             const types::type_env_t &type_env,
                             const translation_env_t &tenv,
                             tracked_types_t &typing,
                             needed_defns_t &needed_defns,
                             bool &returns,
                             translate_continuation_t &matched,
                             translate_continuation_t &failed) const {
  if (!do_checks) {
    return matched(bound_vars, type_env, tenv, typing, needed_defns, returns);
  }

  auto type = tenv.get_type(this);
  assert(type != nullptr);

  auto Bool = type_id(make_iid(BOOL_TYPE));
  var_t *literal_cmp = new var_t(make_iid("std.=="));
  types::type_t::ref cmp_type = type_arrows({type, type, Bool});

  typing[literal_cmp] = cmp_type;
  insert_needed_defn(needed_defns,
                     defn_id_t{literal_cmp->id, cmp_type->generalize({})},
                     token.location, for_defn_id);

  bool truthy_returns = false;
  bool falsey_returns = false;

  auto scrutinee = new var_t(scrutinee_id);
  typing[scrutinee] = type;

  auto cmp_scrutinee = new application_t(literal_cmp, scrutinee);
  typing[cmp_scrutinee] = type_arrows({type, Bool});

  auto literal_value_copy = new literal_t(token);
  typing[literal_value_copy] = type;

  auto condition = new application_t(cmp_scrutinee, literal_value_copy);
  typing[condition] = Bool;

  auto cond = new conditional_t(
      condition,
      matched(bound_vars, type_env, tenv, typing, needed_defns, truthy_returns),
      failed(bound_vars, type_env, tenv, typing, needed_defns, falsey_returns));
  assert(!returns);
  returns = returns || (truthy_returns && falsey_returns);
  assert(typing.count(cond) == 0);
  assert(typing.count(cond->truthy) == 1);
  typing[cond] = typing.at(cond->truthy);
  return cond;
}

expr_t *translate_next(const defn_id_t &for_defn_id,
                       const identifier_t &scrutinee_id,
                       const types::type_t::ref &scrutinee_type,
                       const types::type_t::refs &param_types,
                       bool do_checks,
                       const std::unordered_set<std::string> &bound_vars_,
                       const std::vector<predicate_t *> &params,
                       int param_index,
                       int dim_offset,
                       const types::type_env_t &type_env,
                       const translation_env_t &tenv,
                       tracked_types_t &typing,
                       needed_defns_t &needed_defns,
                       bool &returns,
                       translate_continuation_t &matched,
                       translate_continuation_t &failed) {
  identifier_t param_id = params[param_index]->instantiate_name_assignment();

  auto bound_vars = bound_vars_;
  bound_vars.insert(param_id.name);

  auto matching = [&for_defn_id, param_index, dim_offset, &matched, &failed,
                   &params, &scrutinee_id, &scrutinee_type, &param_types,
                   do_checks](const std::unordered_set<std::string> &bound_vars,
                              const types::type_env_t &type_env,
                              const translation_env_t &tenv,
                              tracked_types_t &typing,
                              needed_defns_t &needed_defns, bool &returns) {
    if (param_index + 1 < params.size()) {
      return translate_next(for_defn_id, scrutinee_id, scrutinee_type,
                            param_types, do_checks, bound_vars, params,
                            param_index + 1, dim_offset, type_env, tenv, typing,
                            needed_defns, returns, matched, failed);
    } else {
      return matched(bound_vars, type_env, tenv, typing, needed_defns, returns);
    }
  };

  auto scrutinee = new var_t(scrutinee_id);
  typing[scrutinee] = scrutinee_type;

  types::type_t::refs tuple_dims;
  for (auto i = 0; i < dim_offset; ++i) {
    // TODO: check whether this makes any sense due to the fragile nature
    // here that the values are all the same size as the unit.
    tuple_dims.push_back(type_unit(INTERNAL_LOC()));
  }
  for (auto param_type : param_types) {
    tuple_dims.push_back(param_type);
  }

  auto as_tuple_type = type_tuple(tuple_dims);
  auto scrutinee_as_tuple = new as_t(scrutinee,
                                     as_tuple_type->generalize({})->normalize(),
                                     true /*force_cast*/);
  typing[scrutinee_as_tuple] = as_tuple_type;

  auto dim = new tuple_deref_t(scrutinee_as_tuple, param_index + dim_offset,
                               0 /*ignored in gen phase*/);
  typing[dim] = param_types[param_index];

  auto body = params[param_index]->translate(
      for_defn_id, param_id, param_types[param_index], do_checks, bound_vars,
      type_env, tenv, typing, needed_defns, returns, matching, failed);
  assert(in(body, typing));

  auto let = new let_t(param_id, dim, body);
  typing[let] = typing[body];
  return let;
}

void ctor_predicate_t::get_bound_vars(
    std::unordered_set<std::string> &bound_vars) const {
  if (name_assignment.valid) {
    bound_vars.insert(name_assignment.t.name);
  }
  for (auto param : params) {
    param->get_bound_vars(bound_vars);
  }
}

expr_t *ctor_predicate_t::translate(
    const defn_id_t &for_defn_id,
    const identifier_t &scrutinee_id,
    const types::type_t::ref &scrutinee_type,
    bool do_checks,
    const std::unordered_set<std::string> &bound_vars,
    const types::type_env_t &type_env,
    const translation_env_t &tenv,
    tracked_types_t &typing,
    needed_defns_t &needed_defns,
    bool &returns,
    translate_continuation_t &matched,
    translate_continuation_t &failed) const {
  static auto Int = type_int(INTERNAL_LOC());
  static auto Bool = type_bool(INTERNAL_LOC());
  types::type_t::refs ctor_terms = tenv.get_data_ctor_terms(scrutinee_type,
                                                            ctor_name);
  assert(ctor_terms.size() >= 1);
  ctor_terms = vec_slice(ctor_terms, 0, ctor_terms.size() - 1);

  if (do_checks) {
    int ctor_id = tenv.get_ctor_id(ctor_name.name);
    var_t *cmp_ctor_id = new var_t(make_iid("__builtin_cmp_ctor_id"));
    typing[cmp_ctor_id] = type_arrows({scrutinee_type, Int, Bool});

    auto ctor_id_literal = new literal_t(
        token_t{location, tk_integer, std::to_string(ctor_id)});
    typing[ctor_id_literal] = Int;

    expr_t *scrutinee = new var_t(scrutinee_id);
    typing[scrutinee] = scrutinee_type;

    auto condition = new builtin_t(cmp_ctor_id, {scrutinee, ctor_id_literal});
    typing[condition] = type_bool(INTERNAL_LOC());

    bool truthy_returns = false;
    bool falsey_returns = false;
    auto cond = new conditional_t(
        condition,
        (params.size() != 0)
            ? translate_next(for_defn_id, scrutinee_id, scrutinee_type,
                             ctor_terms, do_checks, bound_vars, params, 0,
                             1 /*dim_offset*/, type_env, tenv, typing,
                             needed_defns, truthy_returns, matched, failed)
            : matched(bound_vars, type_env, tenv, typing, needed_defns,
                      truthy_returns),
        failed(bound_vars, type_env, tenv, typing, needed_defns,
               falsey_returns));
    typing[cond] = type_unit(INTERNAL_LOC());
    assert(!returns);
    returns = returns || (truthy_returns && falsey_returns);
    return cond;
  } else {
    return (params.size() != 0)
               ? translate_next(for_defn_id, scrutinee_id, scrutinee_type,
                                ctor_terms, do_checks, bound_vars, params, 0,
                                1 /*dim_offset*/, type_env, tenv, typing,
                                needed_defns, returns, matched, failed)
               : matched(bound_vars, type_env, tenv, typing, needed_defns,
                         returns);
  }
}

void tuple_predicate_t::get_bound_vars(
    std::unordered_set<std::string> &bound_vars) const {
  if (name_assignment.valid) {
    bound_vars.insert(name_assignment.t.name);
  }
  for (auto param : params) {
    param->get_bound_vars(bound_vars);
  }
}

expr_t *tuple_predicate_t::translate(
    const defn_id_t &for_defn_id,
    const identifier_t &scrutinee_id,
    const types::type_t::ref &scrutinee_type,
    bool do_checks,
    const std::unordered_set<std::string> &bound_vars,
    const types::type_env_t &type_env,
    const translation_env_t &tenv,
    tracked_types_t &typing,
    needed_defns_t &needed_defns,
    bool &returns,
    translate_continuation_t &matched,
    translate_continuation_t &failed) const {
  auto tuple_type = safe_dyncast<const types::type_tuple_t>(scrutinee_type);

  return (params.size() != 0)
             ? translate_next(for_defn_id, scrutinee_id, scrutinee_type,
                              tuple_type->dimensions, do_checks, bound_vars,
                              params, 0, 0 /*dim_offset*/, type_env, tenv,
                              typing, needed_defns, returns, matched, failed)
             : matched(bound_vars, type_env, tenv, typing, needed_defns,
                       returns);
}

void irrefutable_predicate_t::get_bound_vars(
    std::unordered_set<std::string> &bound_vars) const {
  if (name_assignment.valid) {
    bound_vars.insert(name_assignment.t.name);
  }
}

expr_t *irrefutable_predicate_t::translate(
    const defn_id_t &for_defn_id,
    const identifier_t &scrutinee_id,
    const types::type_t::ref &scrutinee_type,
    bool do_checks,
    const std::unordered_set<std::string> &bound_vars,
    const types::type_env_t &type_env,
    const translation_env_t &tenv,
    tracked_types_t &typing,
    needed_defns_t &needed_defns,
    bool &returns,
    translate_continuation_t &matched,
    translate_continuation_t &) const {
  return matched(bound_vars, type_env, tenv, typing, needed_defns, returns);
}
