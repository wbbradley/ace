#pragma once
#include <memory>

#include "ast.h"
#include "llvm_utils.h"
#include "resolver.h"
#include "types.h"
#include "unification.h"
#include "user_error.h"

namespace gen {

typedef std::unordered_map<
    std::string,
    std::map<types::Ref, std::shared_ptr<Resolver>, types::CompareType>>
    gen_env_t;

typedef std::unordered_map<std::string, llvm::Value *> gen_local_env_t;

llvm::Value *maybe_get_env_var(const gen_env_t &gen_env,
                               std::string name,
                               types::Ref type);
llvm::Value *maybe_get_env_var(const gen_env_t &env,
                               Identifier id,
                               types::Ref type);
llvm::Value *get_env_var(llvm::IRBuilder<> &builder,
                         const gen_env_t &env,
                         Identifier id,
                         types::Ref type);

resolution_status_t gen(std::string name,
                        llvm::IRBuilder<> &builder,
                        llvm::Module *llvm_module,
                        llvm::BasicBlock *break_to_block,
                        llvm::BasicBlock *continue_to_block,
                        const bitter::Expr *expr,
                        const TrackedTypes &typing,
                        const types::TypeEnv &type_env,
                        const gen_env_t &gen_env_globals,
                        const gen_local_env_t &gen_env_locals,
                        const std::unordered_set<std::string> &globals,
                        Publisher *publisher);
} // namespace gen
