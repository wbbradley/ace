#pragma once
#include <map>
#include <string>

#include "gen.h"
#include "llvm_utils.h"
#include "types.h"

namespace lower {
/* the environment during lowering to LLVM */
using lower_env_t = std::map<
    std::string,
    std::map<types::type_t::ref, llvm::Value *, types::compare_type_t>>;

int lower(std::string main_function, const gen::gen_env_t &gen_env);
llvm::Constant *lower_decl(std::string name,
                           llvm::IRBuilder<> &builder,
                           llvm::Module *llvm_module,
                           gen::value_t::ref value,
                           const lower_env_t &env);

llvm::Constant *lower_tuple_global(std::string name,
                                   llvm::IRBuilder<> &builder,
                                   llvm::Module *llvm_module,
                                   gen::tuple_t::ref tuple,
                                   const lower_env_t &env);
} // namespace lower
