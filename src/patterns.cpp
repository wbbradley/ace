#include "patterns.h"

#include <iostream>

#include "ast.h"
#include "builtins.h"
#include "compiler.h"
#include "translate.h"
#include "unification.h"
#include "zion.h"

namespace zion {

using namespace ast;

const Expr *build_patterns(const types::DefnId &for_defn_id,
                           const pattern_blocks_t &pattern_blocks,
                           int index,
                           const std::unordered_set<std::string> &bound_vars_,
                           const types::TypeEnv &type_env,
                           const TranslationEnv &tenv,
                           TrackedTypes &typing,
                           types::NeededDefns &needed_defns,
                           bool &returns,
                           Identifier scrutinee_id,
                           types::Ref scrutinee_type,
                           types::Ref expected_type) {
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

    auto scrutinee = new Var(scrutinee_id);
    typing[scrutinee] = scrutinee_type;
    debug_above(5, log("scrutinee_type of %s is %s", scrutinee->str().c_str(),
                       scrutinee_type->str().c_str()));
    auto expr = new Let(
        scrutinee_id_with_name_assignment, scrutinee,
        pattern_block->predicate->translate(
            for_defn_id, scrutinee_id_with_name_assignment, scrutinee_type,
            do_checks, bound_vars, type_env, tenv, typing, needed_defns,
            returns,
            [&for_defn_id, &pattern_block](
                const std::unordered_set<std::string> &bound_vars,
                const types::TypeEnv &type_env, const TranslationEnv &tenv,
                TrackedTypes &typing, types::NeededDefns &needed_defns,
                bool &returns) -> const Expr * {
              return texpr(for_defn_id, pattern_block->result, bound_vars,
                           tenv.get_type(pattern_block->result), type_env, tenv,
                           typing, needed_defns, returns);
            },
            [index, &pattern_blocks, &for_defn_id, &scrutinee_id,
             &scrutinee_type, &expected_type](
                const std::unordered_set<std::string> &bound_vars,
                const types::TypeEnv &type_env, const TranslationEnv &tenv,
                TrackedTypes &typing, types::NeededDefns &needed_defns,
                bool &returns) -> const Expr * {
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

void check_patterns(Location location,
                    std::string expr,
                    const TranslationEnv &tenv,
                    const pattern_blocks_t &pattern_blocks,
                    types::Ref pattern_value_type) {
  match::Pattern::ref uncovered = match::all_of(
      location, maybe<Identifier>(make_iid(expr)), tenv, pattern_value_type);
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

const Expr *translate_match_expr(
    const types::DefnId &for_defn_id,
    const ast::Match *match,
    const std::unordered_set<std::string> &bound_vars,
    const types::TypeEnv &type_env,
    const TranslationEnv &tenv,
    TrackedTypes &typing,
    types::NeededDefns &needed_defns,
    bool &returns) {
  auto expected_type = tenv.get_type(match);

  debug_above(6, log("match expression is expecting type %s",
                     expected_type->str().c_str()));

  auto scrutinee_expr = texpr(for_defn_id, match->scrutinee, bound_vars,
                              tenv.get_type(match->scrutinee), type_env, tenv,
                              typing, needed_defns, returns);

  if (returns) {
    throw user_error(scrutinee_expr->get_location(),
                     "this value will return so the match seems pointless?");
  }

  auto scrutinee_type = tenv.get_type(match->scrutinee);

  check_patterns(scrutinee_expr->get_location(), match->scrutinee->str(), tenv,
                 match->pattern_blocks, scrutinee_type);

  Identifier scrutinee_id = make_iid("__scrutinee_" + fresh());
  auto new_match = new Let(
      scrutinee_id, scrutinee_expr,
      build_patterns(for_defn_id, match->pattern_blocks, 0, bound_vars,
                     type_env, tenv, typing, needed_defns, returns,
                     scrutinee_id, typing[scrutinee_expr], expected_type));
  typing[new_match] = expected_type;
  return new_match;
}

void Literal::get_bound_vars(
    std::unordered_set<std::string> &bound_vars) const {
}

const Expr *Literal::translate(
    const types::DefnId &for_defn_id,
    const Identifier &scrutinee_id,
    const types::Ref &scrutinee_type,
    bool do_checks,
    const std::unordered_set<std::string> &bound_vars,
    const types::TypeEnv &type_env,
    const TranslationEnv &tenv,
    TrackedTypes &typing,
    types::NeededDefns &needed_defns,
    bool &returns,
    translate_continuation_t &matched,
    translate_continuation_t &failed) const {
  if (!do_checks) {
    return matched(bound_vars, type_env, tenv, typing, needed_defns, returns);
  }

  auto type = tenv.get_type(this);
  assert(type != nullptr);

  auto Bool = type_id(make_iid(BOOL_TYPE));
  Var *literal_cmp = new Var(make_iid("std.=="));
  types::Ref cmp_type = type_arrow(type_params({type, type}), Bool);

  typing[literal_cmp] = cmp_type;
  insert_needed_defn(needed_defns,
                     types::DefnId{literal_cmp->id, cmp_type},
                     token.location, for_defn_id);

  bool truthy_returns = false;
  bool falsey_returns = false;

  auto scrutinee = new Var(scrutinee_id);
  typing[scrutinee] = type;

  auto literal_value_copy = new Literal(token);
  typing[literal_value_copy] = type;

  auto condition = new Application(literal_cmp,
                                   {scrutinee, literal_value_copy});
  typing[condition] = Bool;

  auto cond = new Conditional(
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

const Expr *translate_next(const types::DefnId &for_defn_id,
                           const Identifier &scrutinee_id,
                           const types::Ref &scrutinee_type,
                           const types::Refs &param_types,
                           bool do_checks,
                           const std::unordered_set<std::string> &bound_vars_,
                           const std::vector<const Predicate *> &params,
                           int param_index,
                           int dim_offset,
                           const types::TypeEnv &type_env,
                           const TranslationEnv &tenv,
                           TrackedTypes &typing,
                           types::NeededDefns &needed_defns,
                           bool &returns,
                           translate_continuation_t &matched,
                           translate_continuation_t &failed) {
  Identifier param_id = params[param_index]->instantiate_name_assignment();

  auto bound_vars = bound_vars_;
  bound_vars.insert(param_id.name);

  auto matching = [&for_defn_id, param_index, dim_offset, &matched, &failed,
                   &params, &scrutinee_id, &scrutinee_type, &param_types,
                   do_checks](const std::unordered_set<std::string> &bound_vars,
                              const types::TypeEnv &type_env,
                              const TranslationEnv &tenv, TrackedTypes &typing,
                              types::NeededDefns &needed_defns, bool &returns) {
    if (param_index + 1 < params.size()) {
      return translate_next(for_defn_id, scrutinee_id, scrutinee_type,
                            param_types, do_checks, bound_vars, params,
                            param_index + 1, dim_offset, type_env, tenv, typing,
                            needed_defns, returns, matched, failed);
    } else {
      return matched(bound_vars, type_env, tenv, typing, needed_defns, returns);
    }
  };

  auto scrutinee = new Var(scrutinee_id);
  typing[scrutinee] = scrutinee_type;

  types::Refs tuple_dims;
  for (auto i = 0; i < dim_offset; ++i) {
    // TODO: check whether this makes any sense due to the fragile nature
    // here that the values are all the same size as the unit.
    tuple_dims.push_back(type_unit(INTERNAL_LOC()));
  }
  for (auto param_type : param_types) {
    tuple_dims.push_back(param_type);
  }

  auto as_tuple_type = type_tuple(tuple_dims);
  auto scrutinee_as_tuple = new As(scrutinee, as_tuple_type,
                                   true /*force_cast*/);
  typing[scrutinee_as_tuple] = as_tuple_type;

  auto dim = new TupleDeref(scrutinee_as_tuple, param_index + dim_offset,
                            0 /*ignored in gen phase*/);
  typing[dim] = param_types[param_index];

  auto body = params[param_index]->translate(
      for_defn_id, param_id, param_types[param_index], do_checks, bound_vars,
      type_env, tenv, typing, needed_defns, returns, matching, failed);
  assert(in(body, typing));

  auto let = new Let(param_id, dim, body);
  typing[let] = typing[body];
  return let;
}

void CtorPredicate::get_bound_vars(
    std::unordered_set<std::string> &bound_vars) const {
  if (name_assignment.valid) {
    bound_vars.insert(name_assignment.t.name);
  }
  for (auto param : params) {
    param->get_bound_vars(bound_vars);
  }
}

const Expr *CtorPredicate::translate(
    const types::DefnId &for_defn_id,
    const Identifier &scrutinee_id,
    const types::Ref &scrutinee_type,
    bool do_checks,
    const std::unordered_set<std::string> &bound_vars,
    const types::TypeEnv &type_env,
    const TranslationEnv &tenv,
    TrackedTypes &typing,
    types::NeededDefns &needed_defns,
    bool &returns,
    translate_continuation_t &matched,
    translate_continuation_t &failed) const {
  static auto Int = type_int(INTERNAL_LOC());
  static auto Bool = type_bool(INTERNAL_LOC());
  types::Ref ctor_type = tenv.get_data_ctor_type(scrutinee_type, ctor_name);
  types::Refs ctor_terms = unfold_arrows(ctor_type);

  assert(ctor_terms.size() >= 1);
  ctor_terms = vec_slice(ctor_terms, 0, ctor_terms.size() - 1);

  types::Ref resolved_scrutinee_type = scrutinee_type->eval(type_env);

  debug_above(4,
              log_location(get_location(), "scrutinee type %s resolved to %s",
                           scrutinee_type->str().c_str(),
                           resolved_scrutinee_type->str().c_str()));

  bool just_compare_ints = false;
  if (!type_equality(resolved_scrutinee_type, scrutinee_type)) {
    /* we found a newtype? */
    if (params.size() == 0) {
      /* if there are zero parameters, then we are comparing enums */
      just_compare_ints = true;
    } else if (params.size() == 1) {
      auto scrutinee = new Var(scrutinee_id);
      typing[scrutinee] = scrutinee_type;
      auto casted_scrutinee = new As(scrutinee, resolved_scrutinee_type,
                                     true /*force_cast*/);
      typing[casted_scrutinee] = resolved_scrutinee_type;

      Identifier param_id = params[0]->instantiate_name_assignment();
      auto new_bound_vars = bound_vars;
      new_bound_vars.insert(param_id.name);
      auto let_body = params[0]->translate(
          for_defn_id, param_id, resolved_scrutinee_type, do_checks,
          new_bound_vars, type_env, tenv, typing, needed_defns, returns,
          matched, failed);
      auto casted_pattern_match = new Let(param_id, casted_scrutinee, let_body);
      typing[casted_pattern_match] = typing.at(let_body);
      debug_above(4, log("emitting newtype pattern match %s",
                         casted_pattern_match->str().c_str()));
      return casted_pattern_match;
    } else {
      debug_above(3,
                  log_location(
                      get_location(),
                      "while translating a ctor_predicate. %s != %s and [%s]. "
                      "treating as a newtype tuple",
                      resolved_scrutinee_type->str().c_str(),
                      scrutinee_type->str().c_str(), join_str(params).c_str()));
      auto tuple_type = safe_dyncast<const types::TypeTuple>(
          resolved_scrutinee_type);

      return translate_next(for_defn_id, scrutinee_id, resolved_scrutinee_type,
                            tuple_type->dimensions, do_checks, bound_vars,
                            params, 0, 0 /*dim_offset*/, type_env, tenv, typing,
                            needed_defns, returns, matched, failed);
    }
  }

  if (do_checks) {
    Expr *condition;
    int ctor_id = tenv.get_ctor_id(ctor_name.name);
    auto ctor_id_literal = new Literal(
        Token{location, tk_integer, std::to_string(ctor_id)});
    typing[ctor_id_literal] = Int;

    Expr *scrutinee = new Var(scrutinee_id);
    typing[scrutinee] = scrutinee_type;

    if (just_compare_ints) {
      auto casted_scrutinee = new As(scrutinee, resolved_scrutinee_type,
                                     true /*force_cast*/);
      typing[casted_scrutinee] = resolved_scrutinee_type;

      Var *int_cmp = new Var(make_iid("__builtin_int_eq"));
      typing[int_cmp] = get_builtins().at("__builtin_int_eq")->instantiate({});

      condition = new Builtin(int_cmp, {ctor_id_literal, casted_scrutinee});
      typing[condition] = type_bool(INTERNAL_LOC());
    } else {
      Var *cmp_ctor_id = new Var(make_iid("__builtin_cmp_ctor_id"));
      typing[cmp_ctor_id] = type_arrow(type_params({scrutinee_type, Int}),
                                       Bool);

      condition = new Builtin(cmp_ctor_id, {scrutinee, ctor_id_literal});
      typing[condition] = type_bool(INTERNAL_LOC());
    }

    bool truthy_returns = false;
    bool falsey_returns = false;
    auto match_body = (params.size() != 0)
                          ? translate_next(
                                for_defn_id, scrutinee_id, scrutinee_type,
                                ctor_terms, do_checks, bound_vars, params, 0,
                                1 /*dim_offset*/, type_env, tenv, typing,
                                needed_defns, truthy_returns, matched, failed)
                          : matched(bound_vars, type_env, tenv, typing,
                                    needed_defns, truthy_returns);
    auto cond = new Conditional(condition, match_body,
                                failed(bound_vars, type_env, tenv, typing,
                                       needed_defns, falsey_returns));
    assert(typing.count(match_body) != 0);
    typing[cond] = typing.at(match_body);
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

void TuplePredicate::get_bound_vars(
    std::unordered_set<std::string> &bound_vars) const {
  if (name_assignment.valid) {
    bound_vars.insert(name_assignment.t.name);
  }
  for (auto param : params) {
    param->get_bound_vars(bound_vars);
  }
}

const Expr *TuplePredicate::translate(
    const types::DefnId &for_defn_id,
    const Identifier &scrutinee_id,
    const types::Ref &scrutinee_type,
    bool do_checks,
    const std::unordered_set<std::string> &bound_vars,
    const types::TypeEnv &type_env,
    const TranslationEnv &tenv,
    TrackedTypes &typing,
    types::NeededDefns &needed_defns,
    bool &returns,
    translate_continuation_t &matched,
    translate_continuation_t &failed) const {
  auto tuple_type = safe_dyncast<const types::TypeTuple>(scrutinee_type);

  return (params.size() != 0)
             ? translate_next(for_defn_id, scrutinee_id, scrutinee_type,
                              tuple_type->dimensions, do_checks, bound_vars,
                              params, 0, 0 /*dim_offset*/, type_env, tenv,
                              typing, needed_defns, returns, matched, failed)
             : matched(bound_vars, type_env, tenv, typing, needed_defns,
                       returns);
}

void IrrefutablePredicate::get_bound_vars(
    std::unordered_set<std::string> &bound_vars) const {
  if (name_assignment.valid) {
    bound_vars.insert(name_assignment.t.name);
  }
}

const Expr *IrrefutablePredicate::translate(
    const types::DefnId &for_defn_id,
    const Identifier &scrutinee_id,
    const types::Ref &scrutinee_type,
    bool do_checks,
    const std::unordered_set<std::string> &bound_vars,
    const types::TypeEnv &type_env,
    const TranslationEnv &tenv,
    TrackedTypes &typing,
    types::NeededDefns &needed_defns,
    bool &returns,
    translate_continuation_t &matched,
    translate_continuation_t &) const {
  debug_above(3, log_location(get_location(),
                              "matched irrefutable predicate for %s. "
                              "scrutinee_id = %s :: %s",
                              for_defn_id.str().c_str(),
                              scrutinee_id.str().c_str(),
                              scrutinee_type->str().c_str()));
  return matched(bound_vars, type_env, tenv, typing, needed_defns, returns);
}

} // namespace zion
