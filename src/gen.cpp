#include "gen.h"

#include "ast.h"
#include "ptr.h"
#include "typed_id.h"
#include "types.h"
#include "user_error.h"

namespace gen {

// llvm::IRBuilderBase::InsertPointGuard ipg(builder);
#if 0
struct ip_guard_t {
  builder_t &builder;
  builder_t::saved_state saved;

  ip_guard_t(builder_t &builder) : builder(builder), saved(builder.save_ip()) {
    saved = builder.save_ip();
  }
  ~ip_guard_t() {
    builder.restore_ip(saved);
  }
};
#endif

struct loop_guard_t {
  llvm::BasicBlock *old_break_to_block;
  llvm::BasicBlock *old_continue_to_block;
  llvm::BasicBlock *&break_to_block;
  llvm::BasicBlock *&continue_to_block;

  loop_guard_t(llvm::BasicBlock *new_break_to_block,
               llvm::BasicBlock *new_continue_to_block,
               llvm::BasicBlock *&break_to_block,
               llvm::BasicBlock *&continue_to_block)
      : old_break_to_block(break_to_block),
        old_continue_to_block(continue_to_block),
        break_to_block(break_to_block), continue_to_block(continue_to_block) {
    break_to_block = new_break_to_block;
    continue_to_block = new_continue_to_block;
  }

  ~loop_guard_t() {
    break_to_block = old_break_to_block;
    continue_to_block = old_continue_to_block;
  }
};

struct free_vars_t {
  std::set<typed_id_t> typed_ids;
  int count() const {
    return typed_ids.size();
  }
  void add(identifier_t id, types::type_t::ref type) {
    assert(type != nullptr);
    typed_ids.insert({id, type});
  }
  std::string str() const {
    return string_format("{%s}", join(typed_ids, ", ").c_str());
  }
};

void get_free_vars(const bitter::expr_t *expr,
                   const tracked_types_t &typing,
                   const std::unordered_set<std::string> &bindings,
                   free_vars_t &free_vars) {
  debug_above(7, log("get_free_vars(%s, {%s}, ...)", expr->str().c_str(),
                     join(bindings, ", ").c_str()));
  if (auto literal = dcast<const bitter::literal_t *>(expr)) {
  } else if (auto static_print = dcast<const bitter::static_print_t *>(expr)) {
  } else if (auto var = dcast<const bitter::var_t *>(expr)) {
    if (!in(var->id.name, bindings)) {
      free_vars.add(var->id, get(typing, expr, {}));
    }
  } else if (auto lambda = dcast<const bitter::lambda_t *>(expr)) {
    bool already_has_lambda_var = in(lambda->var.name, free_vars);
    std::unordered_set<std::string> new_bindings; // = bindings;
    new_bindings.insert(lambda->var.name);
    get_free_vars(lambda->body, typing, new_bindings, free_vars);
    /* we should never be adding a bound variable name to the closure if it is
     * being overwritten at a lower scope prior to capture */
    assert_implies(!already_has_lambda_var, !in(lambda->var.name, free_vars));
  } else if (auto application = dcast<const bitter::application_t *>(expr)) {
    get_free_vars(application->a, typing, bindings, free_vars);
    get_free_vars(application->b, typing, bindings, free_vars);
  } else if (auto let = dcast<const bitter::let_t *>(expr)) {
    // TODO: allow let-rec
    bool already_has_let_var = in(let->var.name, free_vars);
    get_free_vars(let->value, typing, bindings, free_vars);
    auto new_bound_vars = bindings;
    new_bound_vars.insert(let->var.name);
    get_free_vars(let->body, typing, new_bound_vars, free_vars);
    /* we should never be adding a bound variable name to the closure if it is
     * being overwritten at a lower scope prior to capture */
    assert_implies(!already_has_let_var, in(let->var.name, free_vars));
  } else if (auto fix = dcast<const bitter::fix_t *>(expr)) {
    get_free_vars(fix->f, typing, bindings, free_vars);
  } else if (auto condition = dcast<const bitter::conditional_t *>(expr)) {
    get_free_vars(condition->cond, typing, bindings, free_vars);
    get_free_vars(condition->truthy, typing, bindings, free_vars);
    get_free_vars(condition->falsey, typing, bindings, free_vars);
  } else if (auto break_ = dcast<const bitter::break_t *>(expr)) {
  } else if (auto while_ = dcast<const bitter::while_t *>(expr)) {
    get_free_vars(while_->condition, typing, bindings, free_vars);
    get_free_vars(while_->block, typing, bindings, free_vars);
  } else if (auto block = dcast<const bitter::block_t *>(expr)) {
    for (auto statement : block->statements) {
      get_free_vars(statement, typing, bindings, free_vars);
    }
  } else if (auto return_ = dcast<const bitter::return_statement_t *>(expr)) {
    get_free_vars(return_->value, typing, bindings, free_vars);
  } else if (auto tuple = dcast<const bitter::tuple_t *>(expr)) {
    for (auto dim : tuple->dims) {
      get_free_vars(dim, typing, bindings, free_vars);
    }
  } else if (auto tuple_deref = dcast<const bitter::tuple_deref_t *>(expr)) {
    get_free_vars(tuple_deref->expr, typing, bindings, free_vars);
  } else if (auto as = dcast<const bitter::as_t *>(expr)) {
    get_free_vars(as->expr, typing, bindings, free_vars);
  } else if (auto sizeof_ = dcast<const bitter::sizeof_t *>(expr)) {
  } else if (auto builtin = dcast<const bitter::builtin_t *>(expr)) {
    for (auto expr : builtin->exprs) {
      get_free_vars(expr, typing, bindings, free_vars);
    }
  } else if (auto match = dcast<const bitter::match_t *>(expr)) {
    get_free_vars(match->scrutinee, typing, bindings, free_vars);
    for (auto pattern_block : match->pattern_blocks) {
      auto new_bindings = bindings;
      pattern_block->predicate->get_bound_vars(new_bindings);
      get_free_vars(pattern_block->result, typing, new_bindings, free_vars);
    }
  } else {
    assert(false);
  }
}

llvm::Value *maybe_get_env_var(const gen_env_t &gen_env,
                               identifier_t id,
                               types::type_t::ref type) {
  type = types::unitize(type);
  return get(gen_env, id.name, type, (llvm::Value *)nullptr);
}

llvm::Value *get_env_var(const gen_env_t &gen_env,
                         identifier_t id,
                         types::type_t::ref type) {
  llvm::Value *llvm_value = maybe_get_env_var(gen_env, id, type);
  if (llvm_value == nullptr) {
    type = types::unitize(type);
    auto error = user_error(id.location, "we need a definition for %s :: %s",
                            id.str().c_str(), type->str().c_str());
    for (auto pair : gen_env) {
      for (auto overload : pair.second) {
        error.add_info(id.location, "%s :: %s = %s", pair.first.c_str(),
                       overload.first->str().c_str(),
                       llvm_print(overload.second).c_str());
      }
    }
    throw error;
  }
  return llvm_value;
}

void set_env_var(gen_env_t &gen_env,
                 std::string name,
                 types::type_t::ref type,
                 llvm::Value *llvm_value,
                 bool allow_shadowing) {
  debug_above(5, log("gen::set_env_var(0x%08llx, %s, %s, %s)",
                     (unsigned long long)(&gen_env), name.c_str(),
                     type->str().c_str(), llvm_print(llvm_value).c_str(),
                     boolstr(allow_shadowing)));
  assert(name.size() != 0);
  type = types::unitize(type);
  llvm::Value *existing_value = get(gen_env, name, type,
                                    (llvm::Value *)nullptr);
  // dbg_when(name == "std.Ref" && type->repr().find("* Char ->") !=
  // std::string::npos);
  if (existing_value == nullptr) {
    debug_above(4, log("found no value in the gen_env 0x%08llx for %s :: %s, "
                       "adding a new value",
                       (unsigned long long)&gen_env, name.c_str(),
                       type->str().c_str()));
    gen_env[name].insert({type, llvm_value});
  } else if (allow_shadowing) {
    debug_above(
        4, log("overwriting any existing value in the gen_env for %s :: %s",
               name.c_str(), type->str().c_str()));
    gen_env[name].insert({type, llvm_value});
  } else {
    /* what now? probably shouldn't have happened */
    assert(false);
  }
  assert(get(gen_env, name, type, (llvm::Value *)nullptr) != nullptr);
}

#if 0
bool is_terminator(instruction_t::ref inst) {
  if (dyncast<return_t>(inst)) {
    return true;
  } else if (dyncast<goto_t>(inst)) {
    return true;
  } else if (dyncast<cond_branch_t>(inst)) {
    return true;
  } else {
    return false;
  }
}

bool has_terminator(instructions_t &instructions) {
  if (instructions.size() == 0) {
    return false;
  } else {
    return is_terminator(instructions.back());
  }
}

void builder_t::ensure_terminator(std::function<void(builder_t &)> callback) {
  assert(block != nullptr);
  if (!has_terminator(block->instructions)) {
    callback(*this);
  }
}

function_t::ref builder_t::create_function(std::string name,
                                           identifiers_t param_ids,
                                           location_t location,
                                           types::type_t::ref type) {
  if (name == "" && function) {
    name = function->name + bitter::fresh();
  }
  auto function = std::make_shared<function_t>(module, name, location, type);
  types::type_t::refs terms;
  unfold_binops_rassoc(ARROW_TYPE_OPERATOR, type, terms);
  assert(terms.size() > param_ids.size());
  for (int i = 0; i < param_ids.size(); ++i) {
    auto param_type = terms[0];
    terms.erase(terms.begin());

    debug_above(8, log("creating argument %s :: %s for %s",
                       param_ids[i].str().c_str(), param_type->str().c_str(),
                       function->get_name().c_str()));
    function->args.push_back(
        std::make_shared<argument_t>(param_ids[i], param_type, i, function));
  }

  set_env_var(module->gen_env, function->get_name(), function);
  return function;
}
#endif

llvm::Value * gen(llvm::IRBuilder<> &builder,
                 llvm::BasicBlock *break_to_block,
                 llvm::BasicBlock *continue_to_block,
                 const bitter::expr_t *expr,
                 const tracked_types_t &typing,
                 const gen_env_t &gen_env,
                 const std::unordered_set<std::string> &globals) {
  return gen("", builder, break_to_block, continue_to_block, expr, typing,
             gen_env, globals);
}

llvm::Value *gen_lambda(std::string name,
                        llvm::IRBuilder<> &builder,
                        const bitter::lambda_t *lambda,
                        types::type_t::ref type,
                        const tracked_types_t &typing,
                        const gen_env_t &gen_env,
                        const std::unordered_set<std::string> &globals) {
  /* see if we need to lift any free variables into a closure */
  free_vars_t free_vars;
  get_free_vars(lambda, typing, globals, free_vars);

  types::type_t::refs type_terms;
  unfold_binops_rassoc(ARROW_TYPE_OPERATOR, type, type_terms);
  /* this function will not be called directly, it will be packaged into a
   * closure. the type system does not reflect the difference between
   * functions or closures, but here when we are lowering, we need to be
   * honest with LLVM. all functions in Zion are capable of taking closure
   * environments, but not all use them. */
  assert(type_terms.size() >= 1);
  auto return_type = type_arrows(type_terms, 1 /*offset*/);
  types::type_t::refs actual_type_terms = {
      type_terms[0], type_id(make_iid("__closure_t")), return_type};

  llvm::Function *llvm_function = llvm_start_function(
      builder, llvm_get_module(builder), actual_type_terms, name);

#if 0
  function_t::ref function = builder.create_function(
      name, {lambda->var}, lambda->get_location(), type);
#endif

  llvm::BasicBlock *block = llvm::BasicBlock::Create(builder.getContext(),
                                                     "entry", llvm_function);
  std::vector<llvm::Value *> llvm_dims;
  types::type_t::refs dim_types;

  /* the closure includes a reference to its code so that it can be run */
  llvm_dims.push_back(llvm_function);

  // CAPTURE
  /* this lambda requires closure over some variables from our environment,
   * and as such requires that we add code to capture the free_vars and place
   * them in our nested environment, but pointing to the closure, not to the
   * outer environment. */
  debug_above(8, log("for %s we need closure by value of %s", name.c_str(),
                     free_vars.str().c_str()));

  for (auto typed_id : free_vars.typed_ids) {
    /* add a copy of each captured variable. If get_env_var fails here, then
     * it means that get_free_vars is talking about a variable that just
     * doesn't exist yet, and thus will need to be captured by a nested
     * closure. */
    llvm_dims.push_back(get_env_var(gen_env, typed_id.id, typed_id.type));
    dim_types.push_back(typed_id.type);
  }

  /* actually copy the available free variables into the closure. if a
   * variable does not exist at this scope, then we rely on a sub-scope that
   * declares that variable to add it to the eventual node that does require
   * the closure.
   *
   * (let f (lambda x (lambda y (lambda z y))))
   *
   * In the above example, the free vars at (lambda x... include y in the
   * innermost (lambda z y), however at this moment, y is not even declared.
   *
   * */
  auto *closure = (llvm_dims.size() == 1 && llvm_dims[0] == llvm_function)
                      ? llvm_create_constant_struct_instance(
                            llvm_create_struct_type(
                                builder, "closure",
                                {llvm_function->getType()->getPointerTo()}),
                            std::vector<llvm::Constant *>({llvm_function}))
                      : llvm_tuple_alloc(builder, llvm_dims);

  llvm::IRBuilderBase::InsertPointGuard ipg(builder);
  builder.SetInsertPoint(block);

  /* put the param in scope */
  auto new_env = gen_env;
  set_env_var(new_env, lambda->var.name, type_terms[0],
              &*llvm_function->args().begin(), true /*allow_shadowing*/);

  llvm::Value *closure_env = builder.CreateBitCast(
      llvm_function->arg_end() - 1, closure->getType(), "closure_env");

  // TODO: consider injecting the lambda's name to avoid recursion issues...
  int arg_index = 1;
  for (auto typed_id : free_vars.typed_ids) {
    // inject the closed over vars into the new environment within the closure
    auto gep_path = std::vector<llvm::Value *>{builder.getInt32(0),
                                               builder.getInt32(arg_index)};
    llvm::Value *llvm_captured_value_in_lambda_scope = builder.CreateLoad(
        builder.CreateInBoundsGEP(closure_env, gep_path));
    llvm_captured_value_in_lambda_scope->setName(typed_id.id.name);
    set_env_var(new_env, typed_id.id.name, dim_types[arg_index - 1],
                llvm_captured_value_in_lambda_scope, true /*allow_shadowing*/);
    ++arg_index;
  }

  /* now build the body of the function */
  gen(builder, nullptr /*break_to_block*/, nullptr /*continue_to_block*/,
      lambda->body, typing, new_env, globals);

  if (builder.GetInsertBlock()->getTerminator() == nullptr) {
    /* ensure that we have a terminator */
    builder.CreatRet(
        llvm::Constant::getNullValue(builder.getInt8Ty()->getPointerTo()));
  }

  return builder.CreateCast(closure->get_location(), closure, function->type,
                            "__closure_as_func_" + bitter::fresh());
}

value_t::ref gen(std::string name,
                 builder_t &builder,
                 llvm::BasicBlock *break_to_block,
                 llvm::BasicBlock *continue_to_block,
                 const bitter::expr_t *expr,
                 const tracked_types_t &typing,
                 const gen_env_t &gen_env,
                 const std::unordered_set<std::string> &globals) {
  try {
    auto type = get(typing, expr, {});
    if (type == nullptr) {
      log_location(log_error, expr->get_location(),
                   "expression lacks typing %s", expr->str().c_str());
      dbg();
    }

    debug_above(8, log("gen(..., %s, ..., ...)", expr->str().c_str()));
    if (auto literal = dcast<const bitter::literal_t *>(expr)) {
      return builder.create_literal(literal->token, type);
    } else if (auto static_print = dcast<const bitter::static_print_t *>(
                   expr)) {
      assert(false);
    } else if (auto var = dcast<const bitter::var_t *>(expr)) {
      return get_env_var(gen_env, var->id, type);
    } else if (auto lambda = dcast<const bitter::lambda_t *>(expr)) {
      return gen_lambda(name, builder, lambda, type, typing, gen_env, globals);
    } else if (auto application = dcast<const bitter::application_t *>(expr)) {
      return builder.create_call(
          gen(builder, break_to_block, continue_to_block, application->a,
              typing, gen_env, globals),
          {gen(builder, application->b, typing, gen_env, globals)}, type, name);
    } else if (auto let = dcast<const bitter::let_t *>(expr)) {
      auto new_env = gen_env;
      auto let_value = gen(builder, break_to_block, continue_to_block,
                           let->value, typing, gen_env, globals);
      set_env_var(new_env, let->var.name, let_value, true /*allow_shadowing*/);
      return gen(builder, continue_to_block, let->body, typing, new_env,
                 globals);
    } else if (auto fix = dcast<const bitter::fix_t *>(expr)) {
      assert(false);
    } else if (auto condition = dcast<const bitter::conditional_t *>(expr)) {
      auto cond = gen(builder, break_to_block, continue_to_block,
                      condition->cond, typing, gen_env, globals);
      block_t::ref truthy_branch = builder.create_block(
          "truthy" + bitter::fresh(), false /*insert_in_new_block*/);
      block_t::ref falsey_branch = builder.create_block(
          "falsey" + bitter::fresh(), false /*insert_in_new_block*/);
      block_t::ref merge_branch;

      builder.create_cond_branch(cond, truthy_branch, falsey_branch);

      builder.set_insertion_block(truthy_branch);
      value_t::ref truthy_value = gen(builder, break_to_block,
                                      continue_to_block, condition->truthy,
                                      typing, gen_env, globals);
      bool truthy_terminates = has_terminator(builder.block->instructions);
      if (!truthy_terminates) {
        merge_branch = builder.create_block("merge" + bitter::fresh(),
                                            false /*insert_in_new_block*/);
        builder.merge_value_into(condition->truthy->get_location(),
                                 truthy_value, merge_branch);
      }

      builder.set_insertion_block(falsey_branch);
      value_t::ref falsey_value = gen(builder, break_to_block,
                                      continue_to_block, condition->falsey,
                                      typing, gen_env, globals);
      bool falsey_terminates = has_terminator(builder.block->instructions);
      if (!falsey_terminates) {
        if (merge_branch == nullptr) {
          merge_branch = builder.create_block("merge" + bitter::fresh(),
                                              false /*insert_in_new_block*/);
        }
        builder.merge_value_into(condition->falsey->get_location(),
                                 falsey_value, merge_branch);
      }

      if (merge_branch != nullptr) {
        builder.set_insertion_block(merge_branch);
      }

      if (auto phi_node = builder.get_current_phi_node()) {
        return phi_node;
      } else {
        return builder.create_unit(INTERNAL_LOC());
      }
    } else if (auto break_ = dcast<const bitter::break_t *>(expr)) {
      assert(builder.break_to_block != nullptr);
      return builder.create_branch(break_->get_location(),
                                   builder.break_to_block);
    } else if (auto continue_ = dcast<const bitter::continue_t *>(expr)) {
      assert(builder.continue_to_block != nullptr);
      return builder.create_branch(continue_->get_location(),
                                   builder.continue_to_block);
    } else if (auto while_ = dcast<const bitter::while_t *>(expr)) {
      auto cond_block = builder.create_block("while_cond" + bitter::fresh(),
                                             false /*insert_in_new_block*/);
      builder.create_branch(while_->get_location(), cond_block);
      builder.set_insertion_block(cond_block);

      auto cond = gen(builder, break_to_block, continue_to_block,
                      while_->condition, typing, gen_env, globals);
      auto while_block = builder.create_block("while_block" + bitter::fresh(),
                                              false /*insert_in_new_block*/);
      auto else_block = builder.create_block("while_break" + bitter::fresh(),
                                             false /*insert_in_new_block*/);

      builder.create_cond_branch(cond, while_block, else_block);
      builder.set_insertion_block(while_block);
      loop_guard_t loop_guard(builder, else_block, cond_block);
      gen(builder, break_to_block, continue_to_block, while_->block, typing,
          gen_env, globals);
      builder.ensure_terminator([cond_block, while_](builder_t &builder) {
        builder.create_branch(while_->get_location(), cond_block);
      });

      builder.set_insertion_block(else_block);
      auto unit_ret = builder.create_unit(while_->get_location());
      // builder.function->render(std::cout) << std::endl;
      // dbg();
      return unit_ret;
    } else if (auto block = dcast<const bitter::block_t *>(expr)) {
      size_t inst_counter = block->statements.size() - 1;

      value_t::ref block_value;
      for (auto statement : block->statements) {
        auto value = gen(builder, break_to_block, continue_to_block, statement,
                         typing, gen_env, globals);
        if (inst_counter == 0) {
          block_value = value;
        }
      }
      return block_value != nullptr
                 ? block_value
                 : builder.create_unit(block->get_location(), name);
    } else if (auto return_ = dcast<const bitter::return_statement_t *>(expr)) {
      return builder.create_return(gen(builder, break_to_block,
                                       continue_to_block, return_->value,
                                       typing, gen_env, globals));
    } else if (auto tuple = dcast<const bitter::tuple_t *>(expr)) {
      std::vector<value_t::ref> dim_values;
      for (auto dim : tuple->dims) {
        dim_values.push_back(gen(builder, dim, typing, gen_env, globals));
      }
      return builder.create_tuple(tuple->get_location(), dim_values, name);
    } else if (auto tuple_deref = dcast<const bitter::tuple_deref_t *>(expr)) {
      auto td = gen(builder, break_to_block, continue_to_block,
                    tuple_deref->expr, typing, gen_env, globals);
      debug_above(10, log_location(tuple_deref->expr->get_location(),
                                   "created tuple deref %s from %s",
                                   td->str().c_str(),
                                   tuple_deref->expr->str().c_str()));
      return builder.create_tuple_deref(tuple_deref->get_location(), td,
                                        tuple_deref->index, name);
    } else if (auto as = dcast<const bitter::as_t *>(expr)) {
      assert(as->force_cast);
      return builder.create_cast(as->get_location(),
                                 gen(builder, break_to_block, continue_to_block,
                                     as->expr, typing, gen_env, globals),
                                 as->scheme->instantiate(INTERNAL_LOC()), name);
    } else if (auto sizeof_ = dcast<const bitter::sizeof_t *>(expr)) {
      assert(false);
    } else if (auto match = dcast<const bitter::match_t *>(expr)) {
      assert(false);
    } else if (auto builtin = dcast<const bitter::builtin_t *>(expr)) {
      std::vector<llvm::Value *> llvm_values;
      for (auto expr : builtin->exprs) {
        llvm_values.push_back(gen(builder, expr, typing, gen_env, globals));
      }
      return lower_builtin(builder, builtin->var->id, llvm_values, type, name);
    }

    throw user_error(expr->get_location(), "unhandled ssa-gen for %s :: %s",
                     expr->str().c_str(), type->str().c_str());
  } catch (user_error &e) {
    e.add_info(expr->get_location(), "while in gen phase for %s",
               expr->str().c_str());
    throw;
  }
}

void builder_t::set_insertion_block(block_t::ref new_block) {
  block = new_block;
  function = new_block->parent.lock();
  module = function->parent.lock();
}

builder_t builder_t::save_ip() const {
  return *this;
}

void builder_t::restore_ip(const builder_t &builder) {
  *this = builder;
}

block_t::ref builder_t::create_block(std::string name,
                                     bool insert_in_new_block) {
  assert(function != nullptr);
  function->blocks.push_back(std::make_shared<block_t>(
      function, name.size() == 0 ? bitter::fresh() : name));
  if (insert_in_new_block) {
    block = function->blocks.back();
  }
  return function->blocks.back();
}

void builder_t::insert_instruction(instruction_t::ref instruction) {
  assert(block != nullptr);
  std::stringstream ss;
  instruction->render(ss);
  // log("adding instruction %s", ss.str().c_str());
  assert(!has_terminator(block->instructions));
  block->instructions.push_back(instruction);
}

void builder_t::merge_value_into(location_t location,
                                 value_t::ref incoming_value,
                                 block_t::ref merge_block) {
  assert(block != nullptr);
  assert(block != merge_block);
  if (!has_terminator(block->instructions)) {
    if (!type_equality(incoming_value->type, type_unit(INTERNAL_LOC()))) {
      phi_node_t::ref phi_node = merge_block->get_phi_node();
      if (phi_node == nullptr) {
        merge_block->instructions.push_front(std::make_shared<phi_node_t>(
            INTERNAL_LOC(), merge_block, incoming_value->type));
        phi_node = safe_dyncast<phi_node_t>(merge_block->instructions.front());
      }
      phi_node->add_incoming_value(incoming_value, block);
    }
    create_branch(location, merge_block);
  }
}

void phi_node_t::add_incoming_value(value_t::ref value,
                                    block_t::ref incoming_block) {
  for (auto pair : incoming_values) {
    if (pair.second == incoming_block) {
      throw user_error(value->get_location(),
                       "there is already a value from this incoming block");
    } else if (pair.first == value) {
      throw user_error(value->get_location(),
                       "this value is being added as an incoming value twice");
    }
  }
  incoming_values.push_back({value, incoming_block});
}

phi_node_t::ref block_t::get_phi_node() {
  if (instructions.size() != 0) {
    if (auto phi_node = dyncast<phi_node_t>(instructions.front())) {
      return phi_node;
    }
  }
  return nullptr;
}

phi_node_t::ref builder_t::get_current_phi_node() {
  assert(block != nullptr);
  return block->get_phi_node();
}

value_t::ref builder_t::create_builtin(identifier_t id,
                                       const value_t::refs &values,
                                       types::type_t::ref type,
                                       std::string name) {
  debug_above(8,
              log("creating builtin %s for %s with type %s", id.str().c_str(),
                  join_str(values, ", ").c_str(), type->str().c_str()));
  auto builtin = std::make_shared<builtin_t>(id.location, block, id, values,
                                             type, name);
  insert_instruction(builtin);
  return builtin;
}

value_t::ref builder_t::create_literal(token_t token, types::type_t::ref type) {
  auto literal = std::make_shared<literal_t>(token, block, type);
  insert_instruction(literal);
  return literal;
}

value_t::ref builder_t::create_call(value_t::ref callable,
                                    const value_t::refs &params,
                                    types::type_t::ref type,
                                    std::string name) {
  auto callsite = std::make_shared<callsite_t>(callable->get_location(), block,
                                               callable, params, name, type);
  insert_instruction(callsite);
  return callsite;
}

value_t::ref builder_t::create_cast(location_t location,
                                    value_t::ref value,
                                    types::type_t::ref type,
                                    std::string name) {
  auto cast = std::make_shared<cast_t>(location, block, value, type, name);
  if (block != nullptr) {
    insert_instruction(cast);
  }
  return cast;
}

value_t::ref builder_t::create_tuple(location_t location,
                                     const std::vector<value_t::ref> &dims,
                                     std::string name) {
  if (dims.size() == 0) {
    return create_unit(location, name);
  } else {
    auto tuple = std::make_shared<gen_tuple_t>(location, block, dims, name);
    if (block != nullptr) {
      insert_instruction(tuple);
    }
    return tuple;
  }
}

value_t::ref builder_t::create_unit(location_t location, std::string name) {
  return std::make_shared<unit_t>(location, block);
}

value_t::ref builder_t::create_tuple_deref(location_t location,
                                           value_t::ref value,
                                           int index,
                                           std::string name) {
  auto tuple_deref = std::make_shared<gen_tuple_deref_t>(location, block, value,
                                                         index, name);
  insert_instruction(tuple_deref);
  return tuple_deref;
}

value_t::ref builder_t::create_branch(location_t location,
                                      block_t::ref goto_block) {
  auto goto_ = std::make_shared<goto_t>(location, block, goto_block);
  insert_instruction(goto_);
  return goto_;
}

value_t::ref builder_t::create_cond_branch(value_t::ref cond,
                                           block_t::ref truthy_branch,
                                           block_t::ref falsey_branch,
                                           std::string name) {
  auto cond_branch = std::make_shared<cond_branch_t>(
      cond->get_location(), block, cond, truthy_branch, falsey_branch, name);
  insert_instruction(cond_branch);
  return cond_branch;
}

value_t::ref builder_t::create_return(value_t::ref expr) {
  auto return_ = std::make_shared<return_t>(expr->get_location(), block, expr);
  insert_instruction(return_);
  return return_;
}

std::ostream &cond_branch_t::render(std::ostream &os) const {
  os << "if " << cond->str() << " then " C_CONTROL "goto " C_RESET;
  return os << truthy_branch->name << " else " C_CONTROL "goto " C_RESET
            << falsey_branch->name;
}

std::ostream &goto_t::render(std::ostream &os) const {
  return os << C_CONTROL "goto " C_RESET << branch->name;
}

std::ostream &callsite_t::render(std::ostream &os) const {
  os << C_ID << name << C_RESET << " := " << callable->str() << "(";
  return os << join_str(params) << ")";
}

std::ostream &phi_node_t::render(std::ostream &os) const {
  os << C_ID << name << C_RESET << " := " << C_WARN "phi" C_RESET "(";
  os << join_with(incoming_values, ", ",
                  [](const std::pair<value_t::ref, block_t::ref> &pair) {
                    return string_format("%s, %s", pair.first->str().c_str(),
                                         pair.second->name.c_str());
                  });
  return os << ")";
}

std::ostream &cast_t::render(std::ostream &os) const {
  return os << C_ID << name << C_RESET
            << " := " << value->str() + " as! " + type->str();
}

std::ostream &load_t::render(std::ostream &os) const {
  return os << C_ID << name << C_RESET << " := load " << rhs->str()
            << " :: " + rhs->type->str();
}

std::ostream &store_t::render(std::ostream &os) const {
  return os << "store "
            << rhs->str() + " :: " + rhs->type->str() + " at address " +
                   lhs->str() + " :: " + lhs->type->str();
}

std::ostream &builtin_t::render(std::ostream &os) const {
  os << C_ID << name << C_RESET << " := " << id.str();
  if (params.size() != 0) {
    os << "(";
    os << join_str(params, ", ");
    os << ")";
  }
  return os;
}

std::ostream &return_t::render(std::ostream &os) const {
  return os << C_CONTROL "return " C_RESET << value->str();
}

std::ostream &literal_t::render(std::ostream &os) const {
  return os << C_ID << name << C_RESET << " := " << token.text
            << " :: " << type->str();
}

std::string argument_t::str() const {
  return C_ID + name + C_RESET;
  // return string_format("arg%d", index);
}

std::ostream &argument_t::render(std::ostream &os) const {
  return os << str();
}

std::string function_t::str() const {
  return C_GOOD "@" + name + C_RESET;
}

std::string block_t::str() const {
  auto function = parent.lock();
  return (function != nullptr) ? (function->name + ":" + name) : name;
}

std::ostream &function_t::render(std::ostream &os) const {
  auto lambda_type = safe_dyncast<const types::type_operator_t>(type);
  types::type_t::refs terms;
  unfold_binops_rassoc(ARROW_TYPE_OPERATOR, type, terms);
  assert(terms.size() > 1);
  auto param_type = terms[0];
  terms.erase(terms.begin());
  auto return_type = type_arrows(terms);

  os << "fn " C_GOOD << name << C_RESET "("
     << join_with(args, ", ", [](const std::shared_ptr<argument_t> &arg) {
          return arg->str() + " :: " + arg->type->str();
        });
  os << ") " << return_type->str();

  if (blocks.size() != 0) {
    os << " {" << std::endl;
    for (auto block : blocks) {
      os << block->name << ":" << std::endl;
      for (auto inst : block->instructions) {
        os << "\t";
        inst->render(os);
        os << std::endl;
      }
    }
    os << "}" << std::endl;
  }
  return os;
}

std::string gen_tuple_t::str() const {
  std::stringstream ss;
  render(ss);
  return ss.str().c_str();
}

std::ostream &gen_tuple_t::render(std::ostream &os) const {
  if (parent.lock() != nullptr) {
    os << C_ID << name << C_RESET << " := ";
  }

  return os << C_GOOD << (parent.lock() ? "alloc_tuple" : "global_tuple")
            << C_RESET "(" << join_str(dims, ", ") << ")";
}

std::ostream &gen_tuple_deref_t::render(std::ostream &os) const {
  return os << C_ID << name << C_RESET " := " << value->str() << "[" << index
            << "] :: " << type->str();
}

std::string instruction_t::str() const {
  return C_ID + name + C_RESET;
}
} // namespace gen
