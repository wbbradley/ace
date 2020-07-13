#include "infer.h"

#include "ast.h"
#include "builtins.h"
#include "dbg.h"
#include "ptr.h"
#include "unification.h"
#include "user_error.h"

namespace zion {

using namespace ast;

namespace {

types::Ref infer_core(const Expr *expr,
                      const DataCtorsMap &data_ctors_map,
                      const types::Ref &return_type,
                      const types::SchemeResolver &scheme_resolver,
                      TrackedTypes &tracked_types,
                      types::Constraints &constraints,
                      types::ClassPredicates &instance_requirements) {
  debug_above(8, log("infer(%s, ..., ...)", expr->str().c_str()));
  if (auto literal = dcast<const Literal *>(expr)) {
    return literal->non_tracking_infer();
  } else if (auto static_print = dcast<const StaticPrint *>(expr)) {
    auto t1 = infer(static_print->expr, data_ctors_map, return_type,
                    scheme_resolver, tracked_types, constraints,
                    instance_requirements);
    append_to_constraints(
        constraints, t1, t1,
        make_context(static_print->get_location(), "to avoid warnings later"));
    return type_unit(static_print->location);
  } else if (auto var = dcast<const Var *>(expr)) {
    /* get a fresh version of this principal type to inject into the context,
     * and the inference constraints */
    types::Scheme::Ref scheme = scheme_resolver.lookup_scheme(var->id)
                                    ->freshen();
    assert(scheme != nullptr);
    debug_above(4, log_location(var->get_location(),
                                "found var ref %s with scheme %s",
                                var->id.str().c_str(),
                                scheme->normalize()->str().c_str()));

    /* ad the related class predicates to this scheme into the mix */
    set_concat(instance_requirements, scheme->predicates);
    return scheme->type;
  } else if (auto lambda = dcast<const Lambda *>(expr)) {
    types::Ref tv = lambda->param_type;
    auto local_return_type = type_variable(lambda->get_location());
    /* lambdas are monomorphic at the time of initialization/definition/capture.
     * so, we do not include the vars |tvs| in the scheme. this way, when the
     * scheme freshens, it will not erase the reference to this variable. */
    types::SchemeResolver local_scheme_resolver(&scheme_resolver);
    local_scheme_resolver.insert_scheme(lambda->var.name, scheme({}, {}, tv));
    auto body_type = infer(lambda->body, data_ctors_map, local_return_type,
                           local_scheme_resolver, tracked_types, constraints,
                           instance_requirements);
    append_to_constraints(constraints, body_type,
                          type_unit(lambda->body->get_location()),
                          make_context(lambda->body->get_location(),
                                       "function body value is not ignored"));
    if (lambda->return_type != nullptr) {
      append_to_constraints(
          constraints, local_return_type, lambda->return_type,
          make_context(lambda->return_type->get_location(),
                       "return type does not match type annotation :: %s",
                       lambda->return_type->str().c_str()));
    }
    return type_arrow(tv, local_return_type);
  } else if (auto application = dcast<const Application *>(expr)) {
    auto t1 = infer(application->a, data_ctors_map, return_type,
                    scheme_resolver, tracked_types, constraints,
                    instance_requirements);
    auto t2 = infer(application->b, data_ctors_map, return_type,
                    scheme_resolver, tracked_types, constraints,
                    instance_requirements);
    auto tv = type_variable(expr->get_location());
    append_to_constraints(
        constraints, t1, type_arrow(application->get_location(), t2, tv),
        make_context(application->get_location(),
                     "(%s :: %s) applied to (%s :: %s) results in type %s",
                     application->a->str().c_str(), t1->str().c_str(),
                     application->b->str().c_str(), t2->str().c_str(),
                     tv->str().c_str()));
    return tv;
  } else if (auto let = dcast<const Let *>(expr)) {
    auto t1 = infer(let->value, data_ctors_map, return_type, scheme_resolver,
                    tracked_types, constraints, instance_requirements);
    auto tv = type_variable(t1->get_location());
    append_to_constraints(
        constraints, tv, t1,
        make_context(let->value->get_location(), "digging deeper..."));

    types::SchemeResolver local_scheme_resolver(&scheme_resolver);
    local_scheme_resolver.insert_scheme(let->var.name, scheme({}, {}, tv));

    auto t2 = infer(let->body, data_ctors_map, return_type,
                    local_scheme_resolver, tracked_types, constraints,
                    instance_requirements);
    debug_above(5, log("the let variable is %s :: %s and the body is %s :: %s",
                       let->var.str().c_str(), tv->str().c_str(),
                       let->body->str().c_str(), t2->str().c_str()));
    return t2;
  } else if (auto condition = dcast<const Conditional *>(expr)) {
    auto t1 = infer(condition->cond, data_ctors_map, return_type,
                    scheme_resolver, tracked_types, constraints,
                    instance_requirements);
    auto t2 = infer(condition->truthy, data_ctors_map, return_type,
                    scheme_resolver, tracked_types, constraints,
                    instance_requirements);
    auto t3 = infer(condition->falsey, data_ctors_map, return_type,
                    scheme_resolver, tracked_types, constraints,
                    instance_requirements);
    append_to_constraints(
        constraints, t1, type_bool(condition->cond->get_location()),
        make_context(condition->get_location(), "conditions must be bool"));
    append_to_constraints(
        constraints, t2, t3,
        make_context(condition->falsey->get_location(),
                     "both branches of conditionals must match types with "
                     "each other"));
    return t2;
  } else if (auto defer = dcast<const Defer *>(expr)) {
    auto t1 = infer(defer->application->a, data_ctors_map, return_type,
                    scheme_resolver, tracked_types, constraints,
                    instance_requirements);
    append_to_constraints(constraints, t1,
                          type_arrows({type_unit(defer->get_location()),
                                       type_unit(defer->get_location())}),
                          make_context(defer->get_location(),
                                       "defer must call nullary function"));

    auto t2 = infer(defer->application->b, data_ctors_map, return_type,
                            scheme_resolver, tracked_types, constraints,
                            instance_requirements);
    append_to_constraints(
        constraints, t1,
        type_arrow(defer->application->get_location(), t2,
                   type_unit(INTERNAL_LOC())),
        make_context(defer->application->get_location(),
                     "deferred application should have type () -> ()"));
    append_to_constraints(
        constraints, t2, type_unit(INTERNAL_LOC()),
        make_context(defer->application->get_location(),
                     "only () may be applied at a deferred callsite"));
    tracked_types[defer->application] = type_unit(INTERNAL_LOC());
    return type_unit(defer->get_location());
  } else if (auto break_ = dcast<const Break *>(expr)) {
    return type_unit(break_->get_location());
  } else if (auto continue_ = dcast<const Continue *>(expr)) {
    return type_unit(continue_->get_location());
  } else if (auto while_ = dcast<const While *>(expr)) {
    auto t1 = infer(while_->condition, data_ctors_map, return_type,
                    scheme_resolver, tracked_types, constraints,
                    instance_requirements);
    append_to_constraints(constraints, t1,
                          type_bool(while_->condition->get_location()),
                          make_context(while_->condition->get_location(),
                                       "while conditions must be bool"));
    auto t2 = infer(while_->block, data_ctors_map, return_type, scheme_resolver,
                    tracked_types, constraints, instance_requirements);
    return type_unit(while_->get_location());
  } else if (auto block = dcast<const Block *>(expr)) {
    types::Ref last_expr_type = type_unit(block->get_location());
    for (size_t i = 0; i < block->statements.size(); ++i) {
      auto expr = block->statements[i];
      auto t1 = infer(expr, data_ctors_map, return_type, scheme_resolver,
                      tracked_types, constraints, instance_requirements);
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
    auto t1 = infer(return_->value, data_ctors_map, return_type,
                    scheme_resolver, tracked_types, constraints,
                    instance_requirements);
    append_to_constraints(
        constraints, t1, return_type,
        make_context(return_->get_location(),
                     "returning (%s " c_good("::") " %s and %s)",
                     return_->value->str().c_str(), t1->str().c_str(),
                     return_type->str().c_str()));
    return type_unit(return_->get_location());
  } else if (auto tuple = dcast<const Tuple *>(expr)) {
    std::vector<types::Ref> dimensions;
    for (auto dim : tuple->dims) {
      dimensions.push_back(infer(dim, data_ctors_map, return_type,
                                 scheme_resolver, tracked_types, constraints,
                                 instance_requirements));
    }
    return type_tuple(tuple->location, dimensions);
  } else if (auto tuple_deref = dcast<const TupleDeref *>(expr)) {
    types::Refs dims;
    for (int i = 0; i < tuple_deref->max; ++i) {
      dims.push_back(type_variable(INTERNAL_LOC()));
    }
    auto t1 = infer(tuple_deref->expr, data_ctors_map, return_type,
                    scheme_resolver, tracked_types, constraints,
                    instance_requirements);
    auto tuple = type_tuple(dims);
    append_to_constraints(constraints, t1, tuple,
                          make_context(expr->get_location(),
                                       "dereferencing tuple index %d of %d",
                                       tuple_deref->index, tuple_deref->max));
    return dims[tuple_deref->index];
  } else if (auto builtin = dcast<const Builtin *>(expr)) {
    types::Refs ts;
    for (auto expr : builtin->exprs) {
      ts.push_back(infer(expr, data_ctors_map, return_type, scheme_resolver,
                         tracked_types, constraints, instance_requirements));
    }
    ts.push_back(type_variable(builtin->get_location()));
    auto t1 = infer(builtin->var, data_ctors_map, return_type, scheme_resolver,
                    tracked_types, constraints, instance_requirements);
    append_to_constraints(constraints, t1, type_builtin_arrows(ts),
                          make_context(builtin->get_location(), "builtin %s",
                                       builtin->var->str().c_str()));
    return ts.back();
  } else if (auto as = dcast<const As *>(expr)) {
    auto t1 = infer(as->expr, data_ctors_map, return_type, scheme_resolver,
                    tracked_types, constraints, instance_requirements);
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
    auto t1 = infer(match->scrutinee, data_ctors_map, return_type,
                    scheme_resolver, tracked_types, constraints,
                    instance_requirements);
    types::Ref match_type;
    for (auto pattern_block : match->pattern_blocks) {
      /* recurse through the pattern_block->predicate to generate more
       * constraints */
      types::SchemeResolver local_scheme_resolver(&scheme_resolver);
      auto tp = pattern_block->predicate->tracking_infer(
          data_ctors_map, return_type, local_scheme_resolver, tracked_types,
          constraints, instance_requirements);
      append_to_constraints(
          constraints, tp, t1,
          make_context(pattern_block->predicate->get_location(),
                       "pattern must match type of scrutinee"));

      auto t2 = infer(pattern_block->result, data_ctors_map, return_type,
                      local_scheme_resolver, tracked_types, constraints,
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

} // namespace

types::Ref infer(const Expr *expr,
                 const DataCtorsMap &data_ctors_map,
                 const types::Ref &return_type,
                 const types::SchemeResolver &scheme_resolver,
                 TrackedTypes &tracked_types,
                 types::Constraints &constraints,
                 types::ClassPredicates &instance_requirements) {
  types::Ref type = infer_core(expr, data_ctors_map, return_type,
                               scheme_resolver, tracked_types, constraints,
                               instance_requirements);
  tracked_types[expr] = type;
  return type;
}

types::Ref Literal::tracking_infer(
    const DataCtorsMap &data_ctors_map,
    const types::Ref &return_type,
    types::SchemeResolver &scheme_resolver,
    TrackedTypes &tracked_types,
    types::Constraints &constraints,
    types::ClassPredicates &instance_requirements) const {
  types::Ref type = non_tracking_infer();
  tracked_types[this] = type;
  return type;
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
    const DataCtorsMap &data_ctors_map,
    const types::Ref &return_type,
    types::SchemeResolver &scheme_resolver,
    TrackedTypes &tracked_types,
    types::Constraints &constraints,
    types::ClassPredicates &instance_requirements) const {
  types::Refs types;
  for (auto param : params) {
    types.push_back(param->tracking_infer(data_ctors_map, return_type,
                                          scheme_resolver, tracked_types,
                                          constraints, instance_requirements));
  }
  auto type = type_tuple(types);
  if (name_assignment.valid) {
    scheme_resolver.insert_scheme(name_assignment.t.name, scheme({}, {}, type));
  }
  return type;
}

types::Ref IrrefutablePredicate::tracking_infer(
    const DataCtorsMap &data_ctors_map,
    const types::Ref &return_type,
    types::SchemeResolver &scheme_resolver,
    TrackedTypes &tracked_types,
    types::Constraints &constraints,
    types::ClassPredicates &instance_requirements) const {
  auto tv = type_variable(location);
  if (name_assignment.valid) {
    scheme_resolver.insert_scheme(name_assignment.t.name, scheme({}, {}, tv));
  }
  return tv;
}

types::Ref CtorPredicate::tracking_infer(
    const DataCtorsMap &data_ctors_map,
    const types::Ref &return_type,
    types::SchemeResolver &scheme_resolver,
    TrackedTypes &tracked_types,
    types::Constraints &constraints,
    types::ClassPredicates &instance_requirements) const {
  types::Ref ctor_type = get_fresh_data_ctor_type(data_ctors_map, ctor_name);

  debug_above(5, log_location(ctor_type->get_location(), "got ctor_type = %s",
                              ctor_type->str().c_str()));

  types::Refs outer_ctor_terms = unfold_arrows(ctor_type);

  assert(outer_ctor_terms.size() >= 1);
  types::Refs ctor_terms;

  if (outer_ctor_terms.size() > 1) {
    assert(outer_ctor_terms.size() == 2);
    if (auto ctor_tuple = dyncast<const types::TypeTuple>(
            outer_ctor_terms[0])) {
      ctor_terms = ctor_tuple->dimensions;
    } else {
      // Handle single parameter data ctors. This only works because we have
      // banished unary tuples.
      ctor_terms.push_back(outer_ctor_terms[0]);
    }
    if (ctor_terms.size() != this->params.size()) {
      throw user_error(
          get_location(),
          "incorrect number of sub-patterns given to %s (%d vs. %d)",
          ctor_name.str().c_str(), ctor_terms.size(), params.size());
    }
  }

  types::Ref result_type;
  for (size_t i = 0; i < params.size(); ++i) {
    auto tp = params[i]->tracking_infer(data_ctors_map, return_type,
                                        scheme_resolver, tracked_types,
                                        constraints, instance_requirements);
    append_to_constraints(constraints, tp, ctor_terms[i],
                          make_context(params[i]->get_location(),
                                       "checking subpattern %s",
                                       params[i]->str().c_str()));
  }
  auto ctor_return_type = outer_ctor_terms.back();
  debug_above(8, log("CtorPredicate::infer(...) -> %s",
                     ctor_return_type->str().c_str()));
  if (name_assignment.valid) {
    scheme_resolver.insert_scheme(name_assignment.t.name,
                                  scheme({}, {}, ctor_return_type));
  }
  return ctor_return_type;
}

} // namespace zion
