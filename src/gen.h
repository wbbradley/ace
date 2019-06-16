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

typedef std::unordered_map<std::string, llvm::Value *> gen_local_env_t;

llvm::Value *maybe_get_env_var(const gen_env_t &gen_env,
                               std::string name,
                               types::type_t::ref type);
llvm::Value *maybe_get_env_var(const gen_env_t &env,
                               identifier_t id,
                               types::type_t::ref type);
llvm::Value *get_env_var(llvm::IRBuilder<> &builder,
                         const gen_env_t &env,
                         identifier_t id,
                         types::type_t::ref type);

resolution_status_t gen(std::string name,
                        llvm::IRBuilder<> &builder,
                        llvm::Module *llvm_module,
                        llvm::BasicBlock *break_to_block,
                        llvm::BasicBlock *continue_to_block,
                        const bitter::expr_t *expr,
                        const tracked_types_t &typing,
                        const types::type_env_t &type_env,
                        const gen_env_t &gen_env_globals,
                        const gen_local_env_t &gen_env_locals,
                        const std::unordered_set<std::string> &globals,
                        publisher_t *publisher);
} // namespace gen
