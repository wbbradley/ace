#include "translate.h"

#include <unordered_set>

#include "ast.h"
#include "builtins.h"
#include "unification.h"
#include "user_error.h"

using namespace bitter;

void check_typing_for_ftvs(const std::string &context,
                           const TrackedTypes &typing) {
  return;
  for (auto pair : typing) {
    if (pair.second->ftv_count() != 0) {
      log("in the context of %s", context.c_str());
      log_location(pair.first->get_location(),
                   "expression %s appears to have unbound type variable(s) "
                   "post-translation :: %s",
                   pair.first->str().c_str(), pair.second->str().c_str());
      dbg();
    }
  }
}

struct TTC {
  /* the typing checker */
  TTC(const std::string &&context, const TrackedTypes &typing)
      : context(std::move(context)), typing(typing) {
  }
  ~TTC() {
    check_typing_for_ftvs(context, typing);
  }

  const std::string context;
  const TrackedTypes &typing;
};

const Expr *texpr(const DefnId &for_defn_id,
                  const bitter::Expr *expr,
                  const std::unordered_set<std::string> &bound_vars,
                  types::Ref type,
                  const types::TypeEnv &type_env,
                  const TranslationEnv &tenv,
                  TrackedTypes &typing,
                  NeededDefns &needed_defns,
                  bool &returns) {
  TTC ttc(string_format("texpr(%s, %s, ..., %s, ...)",
                        for_defn_id.str().c_str(), expr->str().c_str(),
                        type->str().c_str()),
          typing);

  bool starts_already_returned = returns;
  try {
    /* the job of this function is to create a new ast that is constrained to
     * monomorphically typed nodes */
    assert(type != nullptr);
    debug_above(2, log("monomorphizing %s to have type %s", expr->str().c_str(),
                       type->str().c_str()));
#if 0
    if (type->ftv_count() != 0) {
      log_location(expr->get_location(),
                   "cannot monomorphize %s for %s to have type %s because it "
                   "is not fully bound",
                   expr->str().c_str(), for_defn_id.str().c_str(),
                   type->str().c_str());
      dbg();
    }
#endif

    /* check for fully concrete type */
    if (type->generalize({})->btvs() != 0) {
      throw user_error(expr->get_location(),
                       "while (%s) is type-safe, Zion cannot figure out which "
                       "instance within %s to use. please use an 'as' operator "
                       "to add a type hint.",
                       expr->str().c_str(), type->str().c_str());
    }

    if (auto literal = dcast<const Literal *>(expr)) {
      typing[literal] = type;
      return literal;
    } else if (auto static_print = dcast<const StaticPrint *>(expr)) {
      bool fake_returns = false;
      auto inner_expr = texpr(for_defn_id, static_print->expr, bound_vars,
                              tenv.get_type(static_print->expr), type_env, tenv,
                              typing, needed_defns, fake_returns);
      log_location(static_print->expr->get_location(),
                   "within %s the type is %s", for_defn_id.str().c_str(),
                   typing[inner_expr]->str().c_str());
      auto unit_ret = unit_expr(static_print->get_location());
      typing[unit_ret] = type_unit(static_print->get_location());
      return unit_ret;
    } else if (auto var = dcast<const Var *>(expr)) {
      if (!in(var->id.name, bound_vars)) {
        auto defn_id = DefnId{var->id, type->generalize({})->normalize()};
        debug_above(6, log(c_id("%s") " depends on " c_id("%s"),
                           for_defn_id.str().c_str(), defn_id.str().c_str()));
        insert_needed_defn(needed_defns, defn_id, var->get_location(),
                           for_defn_id);
        auto new_var = new Var(var->id);
        typing[new_var] = type;
        return new_var;
      } else {
        typing[var] = type;
        return var;
      }
    } else if (auto lambda = dcast<const Lambda *>(expr)) {
      auto new_bound_vars = bound_vars;
      for (auto &var : lambda->vars) {
        new_bound_vars.insert(var.name);
      }
      bool lambda_returns = false;
      auto new_body = texpr(for_defn_id, lambda->body, new_bound_vars,
                            tenv.get_type(lambda->body), type_env, tenv, typing,
                            needed_defns, lambda_returns);
      types::Refs lambda_terms;
      unfold_binops_rassoc(ARROW_TYPE_OPERATOR, type, lambda_terms);
      assert(lambda_terms.size() >= 2);
      if (!lambda_returns &&
          !unify(lambda_terms.back(), type_unit(INTERNAL_LOC())).result) {
        auto error = user_error(lambda->get_location(),
                                "not all control paths return a value");
        error.add_info(lambda_terms.back()->get_location(), "return type is %s",
                       lambda_terms.back()->str().c_str());
        throw error;
      }
      auto new_lambda = new Lambda(lambda->vars, {}, nullptr, new_body);
      typing[new_lambda] = type;
      return new_lambda;
    } else if (auto application = dcast<const Application *>(expr)) {
      types::Ref operator_type = tenv.get_type(application->a);
      types::Refs operand_types;
      for (auto &param : application->params) {
        operand_types.push_back(tenv.get_type(param));
      }
      types::Ref operand_type = type_params(operand_types);

      /* if we have unresolved types below us in the tree, we need to
       * propagate our known types down into them */
      types::Refs terms;
      unfold_binops_rassoc(ARROW_TYPE_OPERATOR, operator_type, terms);
      assert(terms.size() > 1);

      types::Ref resolution_type = type_arrows({operand_type, type});
      Unification unification = unify(operator_type, resolution_type);
      assert(unification.result);
      operator_type = operator_type->rebind(unification.bindings);

      auto a = texpr(for_defn_id, application->a, bound_vars, operator_type,
                     type_env, tenv, typing, needed_defns, returns);
      std::vector<const Expr *> new_params;
      assert(operand_types.size() == application->params.size());
      for (int i = 0; i < application->params.size(); ++i) {
        /* translate all the parameters */
        auto &param = application->params[i];
        new_params.push_back(
            texpr(for_defn_id, param, bound_vars,
                  operand_types[i]->rebind(unification.bindings), type_env,
                  tenv, typing, needed_defns, returns));
      }
      auto new_app = new Application(a, {new_params});
      typing[new_app] = type;
      return new_app;
    } else if (auto let = dcast<const Let *>(expr)) {
      auto new_value = texpr(for_defn_id, let->value, bound_vars,
                             tenv.get_type(let->value), type_env, tenv, typing,
                             needed_defns, returns);
      auto new_bound_vars = bound_vars;
      new_bound_vars.insert(let->var.name);
      auto new_body = texpr(for_defn_id, let->body, new_bound_vars, type,
                            type_env, tenv, typing, needed_defns, returns);
      auto new_let = new Let(let->var, new_value, new_body);
      typing[new_let] = type;
      return new_let;
    } else if (auto condition = dcast<const Conditional *>(expr)) {
      auto cond = texpr(for_defn_id, condition->cond, bound_vars,
                        tenv.get_type(condition->cond), type_env, tenv, typing,
                        needed_defns, returns);
      bool truthy_returns = false;
      auto truthy = texpr(for_defn_id, condition->truthy, bound_vars,
                          tenv.get_type(condition->truthy), type_env, tenv,
                          typing, needed_defns, truthy_returns);
      bool falsey_returns = false;
      auto falsey = texpr(for_defn_id, condition->falsey, bound_vars,
                          tenv.get_type(condition->falsey), type_env, tenv,
                          typing, needed_defns, falsey_returns);
      if (truthy_returns && falsey_returns) {
        returns = true;
      }
      auto new_conditional = new Conditional(cond, truthy, falsey);
      typing[new_conditional] = type;
      return new_conditional;
    } else if (auto block = dcast<const Block *>(expr)) {
      std::vector<const Expr *> statements;
      for (auto stmt : block->statements) {
        if (returns && !starts_already_returned) {
          throw user_error(stmt->get_location(), "this code will never run");
        }
        statements.push_back(texpr(for_defn_id, stmt, bound_vars,
                                   tenv.get_type(stmt), type_env, tenv, typing,
                                   needed_defns, returns));
      }
      auto new_block = new Block(statements);
      typing[new_block] = type;
      return new_block;
    } else if (auto while_ = dcast<const While *>(expr)) {
      bool block_returns = false;
      auto condition = texpr(for_defn_id, while_->condition, bound_vars,
                             tenv.get_type(while_->condition), type_env, tenv,
                             typing, needed_defns, returns);
      auto block = texpr(for_defn_id, while_->block, bound_vars,
                         tenv.get_type(while_->block), type_env, tenv, typing,
                         needed_defns, block_returns);
      /* NB: we don't really care if the block returns because we can't
       * validate that the loop ever actually runs */
      auto new_while = new While(condition, block);
      typing[new_while] = type;
      return new_while;
    } else if (auto break_ = dcast<const Break *>(expr)) {
      auto new_break = new Break(break_->get_location());
      typing[new_break] = type_unit(INTERNAL_LOC());
      return new_break;
    } else if (auto continue_ = dcast<const Continue *>(expr)) {
      auto new_continue = new Continue(continue_->get_location());
      typing[new_continue] = type_unit(INTERNAL_LOC());
      return new_continue;
    } else if (auto return_ = dcast<const ReturnStatement *>(expr)) {
      auto ret = new ReturnStatement(
          texpr(for_defn_id, return_->value, bound_vars,
                tenv.get_type(return_->value), type_env, tenv, typing,
                needed_defns, returns));
      typing[ret] = type_unit(return_->get_location());
      returns = true;
      return ret;
    } else if (auto tuple = dcast<const Tuple *>(expr)) {
      std::vector<const Expr *> dims;
      for (auto dim : tuple->dims) {
        if (returns && !starts_already_returned) {
          throw user_error(expr->get_location(),
                           "this code will never run due to a prior return");
        }
        dims.push_back(texpr(for_defn_id, dim, bound_vars, tenv.get_type(dim),
                             type_env, tenv, typing, needed_defns, returns));
      }
      auto new_tuple = new Tuple(tuple->get_location(), dims);
      typing[new_tuple] = type;
      return new_tuple;
    } else if (auto match = dcast<const Match *>(expr)) {
      return translate_match_expr(for_defn_id, match, bound_vars, type_env,
                                  tenv, typing, needed_defns, returns);
    } else if (auto as = dcast<const As *>(expr)) {
      auto expr = texpr(for_defn_id, as->expr, bound_vars,
                        as->force_cast ? tenv.get_type(as->expr) : type,
                        type_env, tenv, typing, needed_defns, returns);
      if (as->force_cast) {
        auto new_as = new As(expr, type, true /*force_cast*/);
        typing[new_as] = type;
        return new_as;
      } else {
        /* eliminate non-forceful casts */
        assert(typing.count(expr));
        return expr;
      }
    } else if (auto builtin = dcast<const Builtin *>(expr)) {
      std::vector<const Expr *> exprs;
      for (auto expr : builtin->exprs) {
        exprs.push_back(texpr(for_defn_id, expr, bound_vars,
                              tenv.get_type(expr), type_env, tenv, typing,
                              needed_defns, returns));
      }
      auto new_builtin = new Builtin(
          dynamic_cast<const Var *>(texpr(for_defn_id, builtin->var, bound_vars,
                                          tenv.get_type(builtin->var), type_env,
                                          tenv, typing, needed_defns, returns)),
          exprs);
      typing[new_builtin] = type;
      return new_builtin;
    } else if (auto sizeof_ = dcast<const Sizeof *>(expr)) {
      // TODO(wbbradley): fix this to use the real size
      auto builtin_word_id = Identifier{"__builtin_word_size",
                                        sizeof_->get_location()};
      auto new_sizeof = new Builtin(new Var(builtin_word_id), {});
      typing[new_sizeof] = type;
      return new_sizeof;
    } else if (auto tuple_deref = dcast<const TupleDeref *>(expr)) {
      auto new_tuple_deref = new TupleDeref(
          texpr(for_defn_id, tuple_deref->expr, bound_vars,
                tenv.get_type(tuple_deref->expr), type_env, tenv, typing,
                needed_defns, returns),
          tuple_deref->index, tuple_deref->max);
      typing[new_tuple_deref] = type;
      return new_tuple_deref;
    }
  } catch (user_error &e) {
    auto type = tenv.get_type(expr);
    e.add_info(expr->get_location(), "error while translating %s :: %s",
               expr->str().c_str(), type->str().c_str());
    throw;
  }
  log_location(expr->get_location(), "don't know how to texpr %s",
               expr->str().c_str());
  assert(false);
  return nullptr;
}

Translation::ref translate_expr(
    const DefnId &for_defn_id,
    const bitter::Expr *expr,
    const std::unordered_set<std::string> &bound_vars,
    const types::TypeEnv &type_env,
    const TranslationEnv &tenv,
    NeededDefns &needed_defns,
    bool &returns) {
  TrackedTypes typing;
  const Expr *translated_expr = texpr(for_defn_id, expr, bound_vars,
                                      tenv.get_type(expr), type_env, tenv,
                                      typing, needed_defns, returns);
  return std::make_shared<Translation>(translated_expr, typing);
}

Translation::Translation(const bitter::Expr *expr, const TrackedTypes &typing)
    : expr(expr), typing(typing) {
  check_typing_for_ftvs(std::string("making a Translation"), typing);
}

std::string Translation::str() const {
  return string_format("%s :: %s", expr->str().c_str(),
                       get(typing, expr, {})->str().c_str());
}

Location Translation::get_location() const {
  return expr->get_location();
}

types::Ref TranslationEnv::get_type(const bitter::Expr *e) const {
  auto t = (*tracked_types)[e];
  if (t == nullptr) {
    log_location(log_error, e->get_location(),
                 "translation env does not contain a type for %s",
                 e->str().c_str());
    assert(false && !!"missing type for expression");
  }
  return t;
}

types::Refs TranslationEnv::get_data_ctor_terms(types::Ref type,
                                                Identifier ctor_id) const {
  // TODO: destructure the inbound type operator to find the id and the params.
  // look up the type to find the ctors, then look up the ctor from the inbound
  // ctor_id, and apply the params to the ctor's lambda to get the ctor_type.
  // unfold / destructure the terms of the ctor_type and return that list of
  // terms.

  // types::Refs ctor_terms;
  // unfold_binops_rassoc(ARROW_TYPE_OPERATOR, ctor_type, ctor_terms);

  types::Refs type_terms;
  unfold_ops_lassoc(type, type_terms);
  assert(type_terms.size() != 0);

  auto id = safe_dyncast<const types::TypeId>(type_terms[0]);
  debug_above(7, log("looking for %s in data_ctors_map of size %d",
                     id->str().c_str(), int(data_ctors_map.size())));
  debug_above(8, log("%s", ::str(data_ctors_map).c_str()));
  assert(data_ctors_map.count(id->id.name) != 0);
  auto &data_ctors = data_ctors_map.at(id->id.name);

  auto ctor_type = get(data_ctors, ctor_id.name, {});
  if (ctor_type == nullptr) {
    throw user_error(ctor_id.location, "data ctor %s does not exist",
                     ctor_id.str().c_str());
  }

  debug_above(7, log("starting with ctor_type as %s and type_terms as %s",
                     ctor_type->str().c_str(), ::str(type_terms).c_str()));

  for (int i = 1; i < type_terms.size(); ++i) {
    ctor_type = ctor_type->apply(type_terms[i]);
  }
  debug_above(7, log("resolved ctor_type as %s", ctor_type->str().c_str()));

  types::Refs ctor_terms;
  unfold_binops_rassoc(ARROW_TYPE_OPERATOR, ctor_type, ctor_terms);
  return ctor_terms;
}

std::map<std::string, types::Refs> TranslationEnv::get_data_ctors_terms(
    types::Ref type) const {
  types::Refs type_terms;
  unfold_ops_lassoc(type, type_terms);
  assert(type_terms.size() != 0);

  auto id = safe_dyncast<const types::TypeId>(type_terms[0]);
  debug_above(7, log("looking for %s in data_ctors_map of size %d",
                     id->str().c_str(), int(data_ctors_map.size())));
  debug_above(7, log("%s", ::str(data_ctors_map).c_str()));

  assert(data_ctors_map.count(id->id.name) != 0);
  auto &data_ctors = data_ctors_map.at(id->id.name);

  std::map<std::string, types::Refs> data_ctors_terms;

  for (auto pair : data_ctors) {
    auto ctor_type = pair.second;
    debug_above(7, log("starting with ctor_type as %s and type_terms as %s",
                       ctor_type->str().c_str(), ::str(type_terms).c_str()));

    for (int i = 1; i < type_terms.size(); ++i) {
      ctor_type = ctor_type->apply(type_terms[i]);
    }
    debug_above(7, log("resolved ctor_type as %s", ctor_type->str().c_str()));

    types::Refs ctor_terms;
    unfold_binops_rassoc(ARROW_TYPE_OPERATOR, ctor_type, ctor_terms);

    data_ctors_terms[pair.first] = ctor_terms;
  }
  return data_ctors_terms;
}

types::Refs TranslationEnv::get_fresh_data_ctor_terms(
    Identifier ctor_id) const {
  // FUTURE: build an index to make this faster
  for (auto type_ctors : data_ctors_map) {
    for (auto ctors : type_ctors.second) {
      if (ctors.first == ctor_id.name) {
        types::Ref ctor_type = ctors.second;
        while (true) {
          if (auto type_lambda = dyncast<const types::TypeLambda>(ctor_type)) {
            ctor_type = type_lambda->apply(type_variable(INTERNAL_LOC()));
          } else {
            break;
          }
        }
        types::Refs terms;
        unfold_binops_rassoc(ARROW_TYPE_OPERATOR, ctor_type, terms);
        return terms;
      }
    }
  }
  throw user_error(ctor_id.location, "no data constructor found for %s",
                   ctor_id.str().c_str());
}

int TranslationEnv::get_ctor_id(std::string ctor_name) const {
  auto iter = ctor_id_map.find(ctor_name);
  if (iter == ctor_id_map.end()) {
    throw user_error(INTERNAL_LOC(),
                     "bad ctor name requested during translation (%s)",
                     ctor_name.c_str());
  } else {
    return iter->second;
  }
}
