#include "translate.h"

#include <unordered_set>

#include "ast.h"
#include "builtins.h"
#include "dbg.h"
#include "ptr.h"
#include "unification.h"
#include "user_error.h"

namespace zion {

using namespace ast;

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

const Expr *texpr(const types::DefnId &for_defn_id,
                  const ast::Expr *expr,
                  const DataCtorsMap &data_ctors_map,
                  const std::unordered_set<std::string> &bound_vars,
                  const TrackedTypes &tracked_types,
                  types::Ref type,
                  const types::TypeEnv &type_env,
                  TrackedTypes &typing,
                  types::NeededDefns &needed_defns,
                  // TODO: pass in overloads in order to perform resolution
                  bool &returns) {
  TTC ttc(string_format("texpr(%s, %s, ..., %s, ...)",
                        for_defn_id.str().c_str(), expr->str().c_str(),
                        type->str().c_str()),
          typing);

  type = types::unitize(type);

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
      auto inner_expr = texpr(
          for_defn_id, static_print->expr, data_ctors_map, bound_vars,
          tracked_types, get_tracked_type(tracked_types, static_print->expr),
          type_env, typing, needed_defns, fake_returns);
      log_location(static_print->expr->get_location(),
                   "within %s the type is %s", for_defn_id.str().c_str(),
                   typing[inner_expr]->str().c_str());
      auto unit_ret = unit_expr(static_print->get_location());
      typing[unit_ret] = type_unit(static_print->get_location());
      return unit_ret;
    } else if (auto var = dcast<const Var *>(expr)) {
      if (!in(var->id.name, bound_vars)) {
        types::DefnId defn_id{var->id, type};

        debug_above(2, log(c_id("%s") " depends on " c_id("%s"),
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
      auto new_body = texpr(for_defn_id, lambda->body, data_ctors_map,
                            new_bound_vars, tracked_types,
                            get_tracked_type(tracked_types, lambda->body),
                            type_env, typing, needed_defns, lambda_returns);
      types::Refs lambda_terms = unfold_arrows(type);
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
      types::Ref operator_type = get_tracked_type(tracked_types,
                                                  application->a);
      types::Refs operand_types;
      for (auto &param : application->params) {
        operand_types.push_back(get_tracked_type(tracked_types, param));
      }
      types::Ref operand_type = type_params(operand_types);

      /* if we have unresolved types below us in the tree, we need to
       * propagate our known types down into them */
      types::Refs terms = unfold_arrows(operator_type);
      assert(terms.size() > 1);

      types::Ref resolution_type = type_arrow(operand_type, type);
      types::Unification unification = unify(operator_type, resolution_type);
      assert(unification.result);
      operator_type = operator_type->rebind(unification.bindings);

      auto a = texpr(for_defn_id, application->a, data_ctors_map, bound_vars,
                     tracked_types, operator_type, type_env, typing,
                     needed_defns, returns);
      std::vector<const Expr *> new_params;
      assert(operand_types.size() == application->params.size());
      for (size_t i = 0; i < application->params.size(); ++i) {
        /* translate all the parameters */
        auto &param = application->params[i];
        new_params.push_back(
            texpr(for_defn_id, param, data_ctors_map, bound_vars, tracked_types,
                  operand_types[i]->rebind(unification.bindings), type_env,
                  typing, needed_defns, returns));
      }
      auto new_app = new Application(a, {new_params});
      typing[new_app] = type;
      return new_app;
    } else if (auto let = dcast<const Let *>(expr)) {
      auto new_value = texpr(for_defn_id, let->value, data_ctors_map,
                             bound_vars, tracked_types,
                             get_tracked_type(tracked_types, let->value),
                             type_env, typing, needed_defns, returns);
      auto new_bound_vars = bound_vars;
      new_bound_vars.insert(let->var.name);
      auto new_body = texpr(for_defn_id, let->body, data_ctors_map,
                            new_bound_vars, tracked_types, type, type_env,
                            typing, needed_defns, returns);
      auto new_let = new Let(let->var, new_value, new_body);
      typing[new_let] = type;
      return new_let;
    } else if (auto condition = dcast<const Conditional *>(expr)) {
      auto cond = texpr(for_defn_id, condition->cond, data_ctors_map,
                        bound_vars, tracked_types,
                        get_tracked_type(tracked_types, condition->cond),
                        type_env, typing, needed_defns, returns);
      bool truthy_returns = false;
      auto truthy = texpr(for_defn_id, condition->truthy, data_ctors_map,
                          bound_vars, tracked_types,
                          get_tracked_type(tracked_types, condition->truthy),
                          type_env, typing, needed_defns, truthy_returns);
      bool falsey_returns = false;
      auto falsey = texpr(for_defn_id, condition->falsey, data_ctors_map,
                          bound_vars, tracked_types,
                          get_tracked_type(tracked_types, condition->falsey),
                          type_env, typing, needed_defns, falsey_returns);
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
        statements.push_back(texpr(for_defn_id, stmt, data_ctors_map,
                                   bound_vars, tracked_types,
                                   get_tracked_type(tracked_types, stmt),
                                   type_env, typing, needed_defns, returns));
      }
      auto new_block = new Block(statements);
      typing[new_block] = type;
      return new_block;
    } else if (auto while_ = dcast<const While *>(expr)) {
      bool block_returns = false;
      auto condition = texpr(for_defn_id, while_->condition, data_ctors_map,
                             bound_vars, tracked_types,
                             get_tracked_type(tracked_types, while_->condition),
                             type_env, typing, needed_defns, returns);
      auto block = texpr(for_defn_id, while_->block, data_ctors_map, bound_vars,
                         tracked_types,
                         get_tracked_type(tracked_types, while_->block),
                         type_env, typing, needed_defns, block_returns);
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
          texpr(for_defn_id, return_->value, data_ctors_map, bound_vars,
                tracked_types, get_tracked_type(tracked_types, return_->value),
                type_env, typing, needed_defns, returns));
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
        dims.push_back(texpr(for_defn_id, dim, data_ctors_map, bound_vars,
                             tracked_types,
                             get_tracked_type(tracked_types, dim), type_env,
                             typing, needed_defns, returns));
      }
      auto new_tuple = new Tuple(tuple->get_location(), dims);
      typing[new_tuple] = type;
      return new_tuple;
    } else if (auto match = dcast<const Match *>(expr)) {
      return translate_match_expr(for_defn_id, match, data_ctors_map,
                                  bound_vars, tracked_types, type_env, typing,
                                  needed_defns, returns);
    } else if (auto as = dcast<const As *>(expr)) {
      auto expr = texpr(
          for_defn_id, as->expr, data_ctors_map, bound_vars, tracked_types,
          as->force_cast ? get_tracked_type(tracked_types, as->expr) : type,
          type_env, typing, needed_defns, returns);
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
        exprs.push_back(texpr(for_defn_id, expr, data_ctors_map, bound_vars,
                              tracked_types,
                              get_tracked_type(tracked_types, expr), type_env,
                              typing, needed_defns, returns));
      }
      auto new_builtin = new Builtin(
          dynamic_cast<const Var *>(texpr(
              for_defn_id, builtin->var, data_ctors_map, bound_vars,
              tracked_types, get_tracked_type(tracked_types, builtin->var),
              type_env, typing, needed_defns, returns)),
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
          texpr(for_defn_id, tuple_deref->expr, data_ctors_map, bound_vars,
                tracked_types,
                get_tracked_type(tracked_types, tuple_deref->expr), type_env,
                typing, needed_defns, returns),
          tuple_deref->index, tuple_deref->max);
      typing[new_tuple_deref] = type;
      return new_tuple_deref;
    } else if (auto defer = dcast<const Defer *>(expr)) {
      auto new_deref = new Defer(safe_dcast<const Application>(texpr(
          for_defn_id, defer->application, data_ctors_map, bound_vars,
          tracked_types, get_tracked_type(tracked_types, defer->application),
          type_env, typing, needed_defns, returns)));
      typing[new_deref] = type;
      return new_deref;
    }
  } catch (user_error &e) {
    auto type = get_tracked_type(tracked_types, expr);
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
    const types::DefnId &for_defn_id,
    const ast::Expr *expr,
    const DataCtorsMap &data_ctors_map,
    const std::unordered_set<std::string> &bound_vars,
    const TrackedTypes &tracked_types,
    const types::TypeEnv &type_env,
    types::NeededDefns &needed_defns,
    bool &returns) {
  TrackedTypes typing;
  const Expr *translated_expr = texpr(for_defn_id, expr, data_ctors_map,
                                      bound_vars, tracked_types,
                                      get_tracked_type(tracked_types, expr),
                                      type_env, typing, needed_defns, returns);
  return std::make_shared<Translation>(translated_expr, typing);
}

Translation::Translation(const ast::Expr *expr, const TrackedTypes &typing)
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

} // namespace zion
