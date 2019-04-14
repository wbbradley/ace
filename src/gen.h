#pragma once
#include <memory>

#include "ast.h"
#include "llvm_utils.h"
#include "resolver.h"
#include "types.h"
#include "unification.h"
#include "user_error.h"

namespace gen {

typedef std::unordered_map<std::string,
                           std::map<types::type_t::ref,
                                    std::shared_ptr<resolver_t>,
                                    types::compare_type_t>>
    gen_env_t;

llvm::Value *maybe_get_env_var(const gen_env_t &gen_env,
                               std::string name,
                               types::type_t::ref type);
llvm::Value *maybe_get_env_var(const gen_env_t &env,
                               identifier_t id,
                               types::type_t::ref type);
llvm::Value *get_env_var(const gen_env_t &env,
                         identifier_t id,
                         types::type_t::ref type);

void set_env_var(gen_env_t &gen_env,
                 std::string name,
                 types::type_t::ref type,
                 llvm::Value *llvm_value,
                 bool allow_shadowing);

llvm::Value *gen(std::string name,
                 llvm::IRBuilder<> &builder,
                 llvm::BasicBlock *break_to_block,
                 llvm::BasicBlock *continue_to_block,
                 const bitter::expr_t *expr,
                 const tracked_types_t &typing,
                 gen_env_t &gen_env,
                 const std::unordered_set<std::string> &globals);
} // namespace gen
