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
                           types::type_t::ref type,
                           llvm::IRBuilder<> &builder,
                           llvm::Module *llvm_module,
                           gen::value_t::ref value,
                           lower_env_t &env);

llvm::Constant *lower_tuple_global(std::string name,
                                   llvm::IRBuilder<> &builder,
                                   llvm::Module *llvm_module,
                                   gen::gen_tuple_t::ref tuple,
                                   lower_env_t &env);
llvm::Value *lower_value(
    llvm::IRBuilder<> &builder,
    gen::value_t::ref value,
    std::map<std::string, llvm::Value *> &locals,
    const std::map<gen::block_t::ref,
                   llvm::BasicBlock *,
                   gen::block_t::comparator_t> &block_map,
    std::map<gen::block_t::ref, bool, gen::block_t::comparator_t>
        &blocks_visited,
    lower_env_t &env);

void lower_block(llvm::IRBuilder<> &builder,
                 gen::block_t::ref block,
                 std::map<std::string, llvm::Value *> &locals,
                 const std::map<gen::block_t::ref,
                                llvm::BasicBlock *,
                                gen::block_t::comparator_t> &block_map,
                 std::map<gen::block_t::ref, bool, gen::block_t::comparator_t>
                     &blocks_visited,
                 lower_env_t &env);

} // namespace lower
