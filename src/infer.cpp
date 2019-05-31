#include "infer.h"

#include "ast.h"
#include "builtins.h"
#include "env.h"
#include "unification.h"
#include "user_error.h"

using namespace bitter;

const bool dbg_show_constraints = getenv("ZION_SHOW_CONSTRAINTS") != nullptr;

constraint_t::constraint_t(types::type_t::ref a,
                           types::type_t::ref b,
                           context_t &&context)
    : a(a), b(b), context(std::move(context)) {
}

void append(constraints_t &constraints,
            types::type_t::ref a,
            types::type_t::ref b,
            context_t &&context) {
  if (dbg_show_constraints) {
    log_location(context.location, "constraining a: %s b: %s because %s",
                 a->str().c_str(), b->str().c_str(), context.message.c_str());
    log_location(a->get_location(), "a: %s", a->str().c_str());
    log_location(b->get_location(), "b: %s", b->str().c_str());
  }
  assert(a != nullptr);
  assert(b != nullptr);
  constraints.push_back({a, b, std::move(context)});
}

types::type_t::ref infer_core(expr_t *expr,
                              env_t &env,
                              constraints_t &constraints) {
  debug_above(8, log("infer(%s, ..., ...)", expr->str().c_str()));
  if (auto literal = dcast<literal_t *>(expr)) {
    return literal->non_tracking_infer();
  } else if (auto static_print = dcast<static_print_t *>(expr)) {
    infer(static_print->expr, env, constraints);
    return type_unit(static_print->location);
  } else if (auto var = dcast<var_t *>(expr)) {
    return env.lookup_env(var->id);
  } else if (auto lambda = dcast<lambda_t *>(expr)) {
    auto tv = lambda->param_type != nullptr
                  ? lambda->param_type
                  : type_variable(lambda->var.location);
    auto return_type = type_variable(lambda->var.location);
    auto local_env = env_t{env};
    local_env.return_type = return_type;
    local_env.extend(lambda->var, scheme({}, {}, tv),
                     true /*allow_subscoping*/);
    auto body_type = infer(lambda->body, local_env, constraints);
    if (lambda->return_type != nullptr) {
      append(constraints, return_type, lambda->return_type,
             make_context(lambda->return_type->get_location(),
                          "return type does not match type annotation :: %s",
                          lambda->return_type->str().c_str()));
    }
    return type_arrow(lambda->get_location(), tv, return_type);
  } else if (auto application = dcast<application_t *>(expr)) {
    auto t1 = infer(application->a, env, constraints);
    auto t2 = infer(application->b, env, constraints);
    auto tv = type_variable(expr->get_location());
    append(constraints, t1, type_arrow(application->get_location(), t2, tv),
           make_context(application->get_location(),
                        "(%s :: %s) applied to (%s :: %s) results in type %s",
                        application->a->str().c_str(), t1->str().c_str(),
                        application->b->str().c_str(), t2->str().c_str(),
                        tv->str().c_str()));
    return tv;
  } else if (auto let = dcast<let_t *>(expr)) {
    env_t local_env{env.map,
                    nullptr /*return_type*/,
                    {} /*instance_requirements*/,
                    env.tracked_types,
                    env.ctor_id_map,
                    env.data_ctors_map};

    auto t1 = infer(let->value, local_env, constraints);
    auto tv = type_variable(t1->get_location());
    append(constraints, tv, t1,
           make_context(let->value->get_location(), "digging deeper..."));

    auto body_env = env_t{env};
    body_env.extend(let->var, scheme({}, {}, tv), true /*allow_subscoping*/);
    auto t2 = infer(let->body, body_env, constraints);
    debug_above(3, log("the let variable is %s :: %s and the body is %s :: %s",
                       let->var.str().c_str(), tv->str().c_str(),
                       let->body->str().c_str(), t2->str().c_str()));
    return t2;
  } else if (auto fix = dcast<fix_t *>(expr)) {
    auto tv = type_variable(fix->get_location());
    append(constraints, type_arrow(fix->get_location(), tv, tv),
           infer(fix->f, env, constraints),
           make_context(fix->get_location(), "fixpoint"));
    return tv;
  } else if (auto condition = dcast<conditional_t *>(expr)) {
    auto t1 = infer(condition->cond, env, constraints);
    auto t2 = infer(condition->truthy, env, constraints);
    auto t3 = infer(condition->falsey, env, constraints);
    append(constraints, t1, type_bool(condition->cond->get_location()),
           make_context(condition->get_location(), "conditions must be bool"));
    append(constraints, t2, t3,
           make_context(condition->falsey->get_location(),
                        "both branches of conditionals must match types with "
                        "each other"));
    return t2;
  } else if (auto break_ = dcast<break_t *>(expr)) {
    return type_unit(break_->get_location());
  } else if (auto while_ = dcast<while_t *>(expr)) {
    auto t1 = infer(while_->condition, env, constraints);
    append(constraints, t1, type_bool(while_->condition->get_location()),
           make_context(while_->condition->get_location(),
                        "while conditions must be bool"));
    auto t2 = infer(while_->block, env, constraints);
    return type_unit(while_->get_location());
  } else if (auto block = dcast<block_t *>(expr)) {
    types::type_t::ref last_expr_type = type_unit(block->get_location());
    for (int i = 0; i < block->statements.size(); ++i) {
      auto expr = block->statements[i];
      auto t1 = infer(expr, env, constraints);
      if (i != block->statements.size() - 1) {
        if (auto return_statement = dcast<return_statement_t *>(expr)) {
          throw user_error(return_statement->get_location(),
                           "there are statements after a return statement");
        }
        /* all non-final statements must be unit typed? */
        append(constraints, t1, type_unit(expr->get_location()),
               make_context(expr->get_location(), "value is not ignored"));
      } else {
        last_expr_type = t1;
      }
    }
    return last_expr_type;
  } else if (auto return_ = dcast<return_statement_t *>(expr)) {
    auto t1 = infer(return_->value, env, constraints);
    append(constraints, t1, env.return_type,
           make_context(return_->get_location(),
                        "returning (%s " c_good("::") " %s and %s)",
                        return_->value->str().c_str(), t1->str().c_str(),
                        env.return_type->str().c_str()));
    return type_unit(return_->get_location());
  } else if (auto tuple = dcast<tuple_t *>(expr)) {
    std::vector<types::type_t::ref> dimensions;
    for (auto dim : tuple->dims) {
      dimensions.push_back(infer(dim, env, constraints));
    }
    return type_tuple(tuple->location, dimensions);
  } else if (auto tuple_deref = dcast<tuple_deref_t *>(expr)) {
    types::type_t::refs dims;
    for (int i = 0; i < tuple_deref->max; ++i) {
      dims.push_back(type_variable(INTERNAL_LOC()));
    }
    auto t1 = infer(tuple_deref->expr, env, constraints);
    auto tuple = type_tuple(dims);
    append(constraints, t1, tuple,
           make_context(expr->get_location(),
                        "dereferencing tuple index %d of %d",
                        tuple_deref->index, tuple_deref->max));
    return dims[tuple_deref->index];
  } else if (auto builtin = dcast<builtin_t *>(expr)) {
    types::type_t::refs ts;
    for (auto expr : builtin->exprs) {
      ts.push_back(infer(expr, env, constraints));
    }
    ts.push_back(type_variable(builtin->get_location()));
    auto t1 = infer(builtin->var, env, constraints);
    append(constraints, t1, type_arrows(ts),
           make_context(builtin->get_location(), "builtin %s",
                        builtin->var->str().c_str()));
    return ts.back();
  } else if (auto as = dcast<as_t *>(expr)) {
    auto t1 = infer(as->expr, env, constraints);
    auto as_type = as->scheme->instantiate(as->get_location());
    types::type_t::ref t2 = !as->force_cast ? as_type
                                            : type_variable(as->get_location());
    append(constraints, t1, t2,
           make_context(as->get_location(), "we can get type %s from %s",
                        as->scheme->str().c_str(), as->expr->str().c_str()));
    return as_type;
  } else if (auto sizeof_ = dcast<sizeof_t *>(expr)) {
    return type_id(identifier_t{INT_TYPE, sizeof_->get_location()});
  } else if (auto match = dcast<match_t *>(expr)) {
    auto t1 = infer(match->scrutinee, env, constraints);
    types::type_t::ref match_type;
    for (auto pattern_block : match->pattern_blocks) {
      /* recurse through the pattern_block->predicate to generate more
       * constraints */
      auto local_env = env_t{env};
      auto tp = pattern_block->predicate->tracking_infer(local_env,
                                                         constraints);
      append(constraints, tp, t1,
             make_context(pattern_block->predicate->get_location(),
                          "pattern must match type of scrutinee"));

      auto t2 = infer(pattern_block->result, local_env, constraints);
      if (match_type != nullptr) {
        append(
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

types::type_t::ref infer(expr_t *expr, env_t &env, constraints_t &constraints) {
  return env.track(expr, infer_core(expr, env, constraints));
}

types::type_t::ref literal_t::tracking_infer(env_t &env,
                                             constraints_t &constraints) const {
  return env.track(this, non_tracking_infer());
}

types::type_t::ref literal_t::non_tracking_infer() const {
  switch (token.tk) {
  case tk_integer:
    return type_id(identifier_t{INT_TYPE, token.location});
  case tk_float:
    return type_id(identifier_t{FLOAT_TYPE, token.location});
  case tk_string:
    return type_ptr(type_id(identifier_t{CHAR_TYPE, token.location}));
  case tk_char:
    return type_id(identifier_t{CHAR_TYPE, token.location});
  default:
    throw user_error(token.location, "unsupported type of literal");
  }
}

types::type_t::ref tuple_predicate_t::tracking_infer(
    env_t &env,
    constraints_t &constraints) const {
  types::type_t::refs types;
  for (auto param : params) {
    types.push_back(param->tracking_infer(env, constraints));
  }
  return type_tuple(types);
}

types::type_t::ref irrefutable_predicate_t::tracking_infer(
    env_t &env,
    constraints_t &constraints) const {
  auto tv = type_variable(location);
  if (name_assignment.valid) {
    env.extend(name_assignment.t, scheme({}, {}, tv),
               true /*allow_subscoping*/);
  }
  return tv;
}

types::type_t::ref ctor_predicate_t::tracking_infer(
    env_t &env,
    constraints_t &constraints) const {
  types::type_t::refs ctor_params = env.get_fresh_data_ctor_terms(ctor_name);

  debug_above(8, log("got fresh ctor params %s :: %s", ctor_name.str().c_str(),
                     ::join_str(ctor_params, " -> ").c_str()));

  if (ctor_params.size() - 1 != params.size()) {
    throw user_error(
        get_location(),
        "incorrect number of sub-patterns given to %s (%d vs. %d) %s %s",
        ctor_name.str().c_str(), ctor_params.size() - 1, params.size(),
        ctor_params.back()->str().c_str(),
        ::join_str(ctor_params, ", ").c_str());
  }

  types::type_t::ref result_type;
  for (int i = 0; i < params.size(); ++i) {
    auto tp = params[i]->tracking_infer(env, constraints);
    append(constraints, tp, ctor_params[i],
           make_context(params[i]->get_location(), "checking subpattern %s",
                        params[i]->str().c_str()));
  }

  debug_above(8, log("ctor_predicate_t::infer(...) -> %s",
                     ctor_params.back()->str().c_str()));
  return ctor_params.back();
}

void constraint_t::rebind(const types::type_t::map &env) {
  a = a->rebind(env);
  b = b->rebind(env);
}

std::string constraint_t::str() const {
  return string_format("%s == %s because %s", a->str().c_str(),
                       b->str().c_str(), context.message.c_str());
}

std::string str(const constraints_t &constraints) {
  std::stringstream ss;
  ss << "[";
  const char *delim = "";
  for (auto c : constraints) {
    ss << delim << c.str();
    delim = ", ";
  }
  ss << "]";
  return ss.str();
}
