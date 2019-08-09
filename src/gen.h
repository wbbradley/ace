#pragma once
#include <memory>

#include "ast.h"
#include "llvm_utils.h"
#include "resolver.h"
#include "types.h"
#include "unification.h"
#include "user_error.h"

namespace zion {

namespace gen {

struct DeferGuard;
typedef std::unordered_map<
    std::string,
    std::map<types::Ref, std::shared_ptr<Resolver>, types::CompareType>>
    GenEnv;

typedef std::unordered_map<std::string, llvm::Value *> GenLocalEnv;

llvm::Value *maybe_get_env_var(const GenEnv &gen_env,
                               std::string name,
                               types::Ref type);
llvm::Value *maybe_get_env_var(const GenEnv &env,
                               Identifier id,
                               types::Ref type);
llvm::Value *get_env_var(llvm::IRBuilder<> &builder,
                         const GenEnv &env,
                         Identifier id,
                         types::Ref type);

ResolutionStatus gen(std::string name,
                     llvm::IRBuilder<> &builder,
                     llvm::Module *llvm_module,
                     DeferGuard *defer_guard,
                     llvm::BasicBlock *break_to_block,
                     llvm::BasicBlock *continue_to_block,
                     const ast::Expr *expr,
                     const TrackedTypes &typing,
                     const types::TypeEnv &type_env,
                     const GenEnv &gen_env_globals,
                     const GenLocalEnv &gen_env_locals,
                     const std::unordered_set<std::string> &globals,
                     Publisher *publisher);
} // namespace gen

} // namespace zion
