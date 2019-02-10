#pragma once
#include "zion.h"
#include "llvm_zion.h"
#include "types.h"

llvm::FunctionType *llvm_create_function_type(
		llvm::IRBuilder<> &builder,
		const std::vector<llvm::Type *> &llvm_type_args,
		llvm::Type *llvm_return_type);

llvm::Constant *llvm_create_struct_instance(
		std::string var_name,
		llvm::Module *llvm_module,
		llvm::StructType *llvm_struct_type, 
		std::vector<llvm::Constant *> llvm_struct_data);

llvm::Constant *llvm_create_constant_struct_instance(
		llvm::StructType *llvm_struct_type, 
		std::vector<llvm::Constant *> llvm_struct_data);

std::vector<llvm::Type *> get_llvm_types(const types::type_t::refs &types);
llvm::Type *get_llvm_type(const types::type_t::ref &type);

llvm::Value *llvm_create_bool(llvm::IRBuilder<> &builder, bool value);
llvm::ConstantInt *llvm_create_int(llvm::IRBuilder<> &builder, int64_t value);
llvm::ConstantInt *llvm_create_int32(llvm::IRBuilder<> &builder, int32_t value);
llvm::Value *llvm_zion_bool_to_i1(llvm::IRBuilder<> &builder, llvm::Value *llvm_value);
llvm::Value *llvm_create_double(llvm::IRBuilder<> &builder, double value);
llvm::GlobalVariable *llvm_get_global(llvm::Module *llvm_module, std::string name, llvm::Constant *llvm_constant, bool is_constant);
llvm::Value *llvm_create_global_string(llvm::IRBuilder<> &builder, std::string value);
// bound_var_t::ref create_global_str(llvm::IRBuilder<> &builder, scope_t::ref scope, location_t location, std::string value);
llvm::Module *llvm_get_module(llvm::IRBuilder<> &builder);
llvm::Function *llvm_get_function(llvm::IRBuilder<> &builder);
std::string llvm_print_module(llvm::Module &module);
std::string llvm_print_value(llvm::Value *llvm_value);
std::string llvm_print_type(llvm::Type *llvm_type);
std::string llvm_print(llvm::Value &llvm_value);
std::string llvm_print(llvm::Value *llvm_value);
std::string llvm_print(llvm::Type *llvm_type);
std::string llvm_print_function(llvm::Function *llvm_function);

// TODO: consider consolidating mem management into just one of these...
llvm::AllocaInst *llvm_create_entry_block_alloca(llvm::Function *llvm_function, types::type_t::ref type, std::string var_name);
llvm::AllocaInst *llvm_call_gcroot(llvm::Function *llvm_function, types::type_t::ref type, std::string var_name);

llvm::Value *_llvm_resolve_alloca(llvm::IRBuilder<> &builder, llvm::Value *llvm_value);
llvm::Type *llvm_resolve_type(llvm::Value *llvm_value);
llvm::StructType *llvm_create_struct_type(llvm::IRBuilder<> &builder, std::string name, const types::type_t::refs &dimensions);
llvm::StructType *llvm_create_struct_type(llvm::IRBuilder<> &builder, std::string name, const std::vector<llvm::Type*> &llvm_types);
llvm::Constant *llvm_sizeof_type(llvm::IRBuilder<> &builder, llvm::Type *llvm_type);
llvm::Value *llvm_maybe_pointer_cast(llvm::IRBuilder<> &builder, llvm::Value *llvm_value, llvm::Type *llvm_type);
llvm::Value *llvm_maybe_pointer_cast(llvm::IRBuilder<> &builder, llvm::Value *llvm_value, const types::type_t::ref &bound_type);
llvm::Value *llvm_int_cast(llvm::IRBuilder<> &builder, llvm::Value *llvm_value, llvm::Type *llvm_type);
llvm::Constant *llvm_get_pointer_to_constant(llvm::IRBuilder<> &builder, llvm::Constant *llvm_constant);
llvm::Value *llvm_last_param(llvm::Function *llvm_function);

void check_struct_initialization(
		llvm::ArrayRef<llvm::Constant*> llvm_struct_initialization,
		llvm::StructType *llvm_struct_type);

void llvm_verify_function(location_t location, llvm::Function *llvm_function);
void llvm_verify_module(llvm::Module &llvm_module);

/* flags for llvm_create_if_branch that tell it whether to invoke release_vars
 * for either branch */

struct life_t;

#define IFF_THEN 1
#define IFF_ELSE 2
#define IFF_BOTH (IFF_ELSE | IFF_THEN)

void llvm_create_if_branch(
		llvm::IRBuilder<> &builder,
		int iff,
		std::shared_ptr<life_t> life,
		location_t location,
		llvm::Value *llvm_value,
        bool allow_maybe_check,
		llvm::BasicBlock *then_bb,
		llvm::BasicBlock *else_bb);

llvm::Type *llvm_deref_type(llvm::Type *llvm_pointer_type);
llvm::Function *llvm_start_function(
		llvm::IRBuilder<> &builder, 
		const types::type_t::refs &terms,
		std::string name);

// NOTE: the explain function is a tool to learn about LLVM types, it does not
// handle cyclic types, so it should only be used for debugging.
void explain(llvm::Type *llvm_type);

bool llvm_value_is_handle(llvm::Value *llvm_value);
bool llvm_value_is_pointer(llvm::Value *llvm_value);
llvm::StructType *llvm_find_struct(llvm::Type *llvm_type);
void llvm_generate_dead_return(llvm::IRBuilder<> &builder);
llvm::Instruction *llvm_dead_return(llvm::IRBuilder<> &builder, llvm::Type *llvm_return_type);
