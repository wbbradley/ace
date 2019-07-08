#include "infer.h"

#include "ast.h"
#include "builtins.h"
#include "env.h"
#include "unification.h"
#include "user_error.h"

using namespace bitter;

types::Ref infer_core(const Expr *expr,
                      Env &env,
                      Constraints &constraints,
                      types::ClassPredicates &instance_requirements) {
  debug_above(8, log("infer(%s, ..., ...)", expr->str().c_str()));
  if (auto literal = dcast<const Literal *>(expr)) {
    return literal->non_tracking_infer();
  } else if (auto static_print = dcast<const StaticPrint *>(expr)) {
    auto t1 = infer(static_print->expr, env, constraints,
                    instance_requirements);
    append_to_constraints(
        constraints, t1, t1,
        make_context(static_print->get_location(), "to avoid warnings later"));
    return type_unit(static_print->location);
  } else if (auto var = dcast<const Var *>(expr)) {
    /* get a fresh version of this principal type to inject into the context,
     * and the inference constraints */
    types::Scheme::Ref scheme = env.lookup_env(var->id)->freshen();
    assert(scheme != nullptr);
    debug_above(4, log_location(var->get_location(),
                                "found var ref %s with scheme %s",
                                var->id.str().c_str(),
                                scheme->normalize()->str().c_str()));

    /* ad the related class predicates to this scheme into the mix */
    set_concat(instance_requirements, scheme->predicates);
    return scheme->type;
  } else if (auto lambda = dcast<const Lambda *>(expr)) {
    types::Refs tvs;
    int i = 0;
    assert(lambda->param_types.size() == lambda->vars.size());
    for (auto &param_type : lambda->param_types) {
      assert(param_type != nullptr);
      tvs.push_back(param_type);
    }
    auto return_type = type_variable(lambda->get_location());
    Env local_env = Env{env};
    local_env.return_type = return_type;
    /* lambdas are monomorphic at the time of initialization/definition/capture.
     * so, we do not include the vars |tvs| in the scheme. this way, when the
     * scheme freshens, it will not erase the reference to this variable. */
    i = 0;
    for (auto &var : lambda->vars) {
      local_env.extend(var, scheme({}, {}, tvs[i++]),
                       true /*allow_subscoping*/);
    }
    auto body_type = infer(lambda->body, local_env, constraints,
                           instance_requirements);
    append_to_constraints(constraints, body_type,
                          type_unit(lambda->body->get_location()),
                          make_context(lambda->body->get_location(),
                                       "function body value is not ignored"));
    if (lambda->return_type != nullptr) {
      append_to_constraints(
          constraints, return_type, lambda->return_type,
          make_context(lambda->return_type->get_location(),
                       "return type does not match type annotation :: %s",
                       lambda->return_type->str().c_str()));
    }
    return type_arrow(type_params(tvs), return_type);
  } else if (auto application = dcast<const Application *>(expr)) {
    auto t1 = infer(application->a, env, constraints, instance_requirements);
    std::vector<types::Ref> param_types;
    param_types.reserve(application->params.size());
    for (auto &param : application->params) {
      param_types.push_back(
          infer(param, env, constraints, instance_requirements));
    }
    auto t2 = type_params(param_types);
    auto tv = type_variable(expr->get_location());
    append_to_constraints(
        constraints, t1, type_arrow(application->get_location(), t2, tv),
        make_context(application->get_location(),
                     "(%s :: %s) applied to ((%s) :: %s) results in type %s",
                     application->a->str().c_str(), t1->str().c_str(),
                     join_str(application->params, ", ").c_str(),
                     t2->str().c_str(), tv->str().c_str()));
    return tv;
  } else if (auto let = dcast<const Let *>(expr)) {
    Env local_env{env.map, nullptr /*return_type*/, env.tracked_types,
                  env.ctor_id_map, env.data_ctors_map};

    auto t1 = infer(let->value, local_env, constraints, instance_requirements);
    auto tv = type_variable(t1->get_location());
    append_to_constraints(
        constraints, tv, t1,
        make_context(let->value->get_location(), "digging deeper..."));

    Env body_env = Env{env};
    body_env.extend(let->var, scheme({}, {}, tv), true /*allow_subscoping*/);
    auto t2 = infer(let->body, body_env, constraints, instance_requirements);
    debug_above(3, log("the let variable is %s :: %s and the body is %s :: %s",
                       let->var.str().c_str(), tv->str().c_str(),
                       let->body->str().c_str(), t2->str().c_str()));
    return t2;
  } else if (auto condition = dcast<const Conditional *>(expr)) {
    auto t1 = infer(condition->cond, env, constraints, instance_requirements);
    auto t2 = infer(condition->truthy, env, constraints, instance_requirements);
    auto t3 = infer(condition->falsey, env, constraints, instance_requirements);
    append_to_constraints(
        constraints, t1, type_bool(condition->cond->get_location()),
        make_context(condition->get_location(), "conditions must be bool"));
    append_to_constraints(
        constraints, t2, t3,
        make_context(condition->falsey->get_location(),
                     "both branches of conditionals must match types with "
                     "each other"));
    return t2;
  } else if (auto break_ = dcast<const Break *>(expr)) {
    return type_unit(break_->get_location());
  } else if (auto continue_ = dcast<const Continue *>(expr)) {
    return type_unit(continue_->get_location());
  } else if (auto while_ = dcast<const While *>(expr)) {
    auto t1 = infer(while_->condition, env, constraints, instance_requirements);
    append_to_constraints(constraints, t1,
                          type_bool(while_->condition->get_location()),
                          make_context(while_->condition->get_location(),
                                       "while conditions must be bool"));
    auto t2 = infer(while_->block, env, constraints, instance_requirements);
    return type_unit(while_->get_location());
  } else if (auto block = dcast<const Block *>(expr)) {
    types::Ref last_expr_type = type_unit(block->get_location());
    for (int i = 0; i < block->statements.size(); ++i) {
      auto expr = block->statements[i];
      auto t1 = infer(expr, env, constraints, instance_requirements);
      if (i != block->statements.size() - 1) {
        if (auto return_statement = dcast<const ReturnStatement *>(expr)) {
          throw user_error(return_statement->get_location(),
                           "there are statements after a return statement");
        }
        /* all non-final statements must be unit typed? */
        append_to_constraints(
            constraints, t1, type_unit(expr->get_location()),
            make_context(expr->get_location(), "value is not ignored"));
      } else {
        last_expr_type = t1;
      }
    }
    return last_expr_type;
  } else if (auto return_ = dcast<const ReturnStatement *>(expr)) {
    auto t1 = infer(return_->value, env, constraints, instance_requirements);
    append_to_constraints(
        constraints, t1, env.return_type,
        make_context(return_->get_location(),
                     "returning (%s " c_good("::") " %s and %s)",
                     return_->value->str().c_str(), t1->str().c_str(),
                     env.return_type->str().c_str()));
    return type_unit(return_->get_location());
  } else if (auto tuple = dcast<const Tuple *>(expr)) {
    std::vector<types::Ref> dimensions;
    for (auto dim : tuple->dims) {
      dimensions.push_back(infer(dim, env, constraints, instance_requirements));
    }
    return type_tuple(tuple->location, dimensions);
  } else if (auto tuple_deref = dcast<const TupleDeref *>(expr)) {
    types::Refs dims;
    for (int i = 0; i < tuple_deref->max; ++i) {
      dims.push_back(type_variable(INTERNAL_LOC()));
    }
    auto t1 = infer(tuple_deref->expr, env, constraints, instance_requirements);
    auto tuple = type_tuple(dims);
    append_to_constraints(constraints, t1, tuple,
                          make_context(expr->get_location(),
                                       "dereferencing tuple index %d of %d",
                                       tuple_deref->index, tuple_deref->max));
    return dims[tuple_deref->index];
  } else if (auto builtin = dcast<const Builtin *>(expr)) {
    types::Refs ts;
    for (auto expr : builtin->exprs) {
      ts.push_back(infer(expr, env, constraints, instance_requirements));
    }
    ts.push_back(type_variable(builtin->get_location()));
    auto t1 = infer(builtin->var, env, constraints, instance_requirements);
    append_to_constraints(constraints, t1, type_arrows(ts),
                          make_context(builtin->get_location(), "builtin %s",
                                       builtin->var->str().c_str()));
    return ts.back();
  } else if (auto as = dcast<const As *>(expr)) {
    auto t1 = infer(as->expr, env, constraints, instance_requirements);
    types::Ref t2 = !as->force_cast ? as->type
                                    : type_variable(as->get_location());
    append_to_constraints(
        constraints, t1, t2,
        make_context(as->get_location(), "we can get type %s from %s",
                     as->type->str().c_str(), as->type->str().c_str()));
    return as->type;
  } else if (auto sizeof_ = dcast<const Sizeof *>(expr)) {
    return type_id(Identifier{INT_TYPE, sizeof_->get_location()});
  } else if (auto match = dcast<const Match *>(expr)) {
    auto t1 = infer(match->scrutinee, env, constraints, instance_requirements);
    types::Ref match_type;
    for (auto pattern_block : match->pattern_blocks) {
      /* recurse through the pattern_block->predicate to generate more
       * constraints */
      auto local_env = Env{env};
      auto tp = pattern_block->predicate->tracking_infer(local_env, constraints,
                                                         instance_requirements);
      append_to_constraints(
          constraints, tp, t1,
          make_context(pattern_block->predicate->get_location(),
                       "pattern must match type of scrutinee"));

      auto t2 = infer(pattern_block->result, local_env, constraints,
                      instance_requirements);
      if (match_type != nullptr) {
        append_to_constraints(
            constraints, t2, match_type,
            make_context(pattern_block->result->get_location(),
                         "match pattern blocks must all have the same type"));
      } else {
        match_type = t2;
      }
    }
    assert(match_type != nullptr);
    return match_type;
  }

  throw user_error(expr->get_location(), "unhandled inference for %s",
                   expr->str().c_str());
}

types::Ref infer(const Expr *expr,
                 Env &env,
                 Constraints &constraints,
                 types::ClassPredicates &instance_requirements) {
  return env.track(expr,
                   infer_core(expr, env, constraints, instance_requirements));
}

types::Ref Literal::tracking_infer(
    Env &env,
    Constraints &constraints,
    types::ClassPredicates &instance_requirements) const {
  return env.track(this, non_tracking_infer());
}

types::Ref Literal::non_tracking_infer() const {
  switch (token.tk) {
  case tk_integer:
    return type_id(Identifier{INT_TYPE, token.location});
  case tk_float:
    return type_id(Identifier{FLOAT_TYPE, token.location});
  case tk_string:
    return type_ptr(type_id(Identifier{CHAR_TYPE, token.location}));
  case tk_char:
    return type_id(Identifier{CHAR_TYPE, token.location});
  default:
    throw user_error(token.location, "unsupported type of literal");
  }
}

types::Ref TuplePredicate::tracking_infer(
    Env &env,
    Constraints &constraints,
    types::ClassPredicates &instance_requirements) const {
  types::Refs types;
  for (auto param : params) {
    types.push_back(
        param->tracking_infer(env, constraints, instance_requirements));
  }
  return type_tuple(types);
}

types::Ref IrrefutablePredicate::tracking_infer(
    Env &env,
    Constraints &constraints,
    types::ClassPredicates &instance_requirements) const {
  auto tv = type_variable(location);
  if (name_assignment.valid) {
    env.extend(name_assignment.t, scheme({}, {}, tv),
               true /*allow_subscoping*/);
  }
  return tv;
}

types::Ref CtorPredicate::tracking_infer(
    Env &env,
    Constraints &constraints,
    types::ClassPredicates &instance_requirements) const {
  types::Ref ctor_type = env.get_fresh_data_ctor_type(ctor_name);

  debug_above(5, log_location(ctor_type->get_location(), "got ctor_type = %s",
                              ctor_type->str().c_str()));

  types::Refs ctor_terms = unfold_arrows(ctor_type);

  assert(ctor_terms.size() >= 1);
  if (ctor_terms.size() - 1 != params.size()) {
    throw user_error(get_location(),
                     "incorrect number of sub-patterns given to %s (%d vs. %d)",
                     ctor_name.str().c_str(), ctor_terms.size() - 1,
                     params.size());
  }

  types::Ref result_type;
  for (int i = 0; i < params.size(); ++i) {
    auto tp = params[i]->tracking_infer(env, constraints,
                                        instance_requirements);
    append_to_constraints(constraints, tp, ctor_terms[i],
                          make_context(params[i]->get_location(),
                                       "checking subpattern %s",
                                       params[i]->str().c_str()));
  }

  debug_above(8, log("CtorPredicate::infer(...) -> %s",
                     ctor_terms.back()->str().c_str()));
  return ctor_terms.back();
}
