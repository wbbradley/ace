#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "bound_var.h"
#include "scopes.h"
#include "life.h"

#define DTOR_FN_INDEX 1

extern const char *GC_STRATEGY;

struct compiler_t;
struct life_t;

llvm::FunctionType *llvm_create_function_type(
		llvm::IRBuilder<> &builder,
		const bound_type_t::refs &args,
		bound_type_t::ref return_value);

bound_var_t::ref create_callsite(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		ptr<life_t> life,
		ptr<const bound_var_t> callee,
		std::string name,
		const location_t &location,
		bound_var_t::refs values);

llvm::CallInst *llvm_create_call_inst(
		llvm::IRBuilder<> &builder,
		location_t location,
		ptr<const bound_var_t> callee,
		std::vector<llvm::Value *> llvm_values);

llvm::Constant *llvm_create_struct_instance(
		std::string var_name,
		llvm::Module *llvm_module,
		llvm::StructType *llvm_struct_type, 
		std::vector<llvm::Constant *> llvm_struct_data);

llvm::Constant *llvm_create_constant_struct_instance(
		llvm::StructType *llvm_struct_type, 
		std::vector<llvm::Constant *> llvm_struct_data);

llvm::Value *llvm_create_bool(llvm::IRBuilder<> &builder, bool value);
llvm::ConstantInt *llvm_create_int(llvm::IRBuilder<> &builder, int64_t value);
llvm::ConstantInt *llvm_create_int32(llvm::IRBuilder<> &builder, int32_t value);
llvm::Value *llvm_create_double(llvm::IRBuilder<> &builder, double value);
llvm::GlobalVariable *llvm_get_global(llvm::Module *llvm_module, std::string name, llvm::Constant *llvm_constant, bool is_constant);
llvm::Value *llvm_create_global_string(llvm::IRBuilder<> &builder, std::string value);
bound_var_t::ref create_global_str(llvm::IRBuilder<> &builder, scope_t::ref scope, location_t location, std::string value);
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
llvm::AllocaInst *llvm_create_entry_block_alloca(llvm::Function *llvm_function, bound_type_t::ref type, std::string var_name);
llvm::AllocaInst *llvm_call_gcroot(llvm::Function *llvm_function, bound_type_t::ref type, std::string var_name);

llvm::Value *_llvm_resolve_alloca(llvm::IRBuilder<> &builder, llvm::Value *llvm_value);
llvm::Type *llvm_resolve_type(llvm::Value *llvm_value);
llvm::StructType *llvm_create_struct_type(llvm::IRBuilder<> &builder, std::string name, const bound_type_t::refs &dimensions);
llvm::StructType *llvm_create_struct_type(llvm::IRBuilder<> &builder, std::string name, const std::vector<llvm::Type*> &llvm_types);
llvm::Constant *llvm_sizeof_type(llvm::IRBuilder<> &builder, llvm::Type *llvm_type);
llvm::Value *llvm_maybe_pointer_cast(llvm::IRBuilder<> &builder, llvm::Value *llvm_value, llvm::Type *llvm_type);
llvm::Value *llvm_maybe_pointer_cast(llvm::IRBuilder<> &builder, llvm::Value *llvm_value, const bound_type_t::ref &bound_type);
llvm::Value *llvm_int_cast(llvm::IRBuilder<> &builder, llvm::Value *llvm_value, llvm::Type *llvm_type);
llvm::Constant *llvm_get_pointer_to_constant(llvm::IRBuilder<> &builder, llvm::Constant *llvm_constant);
llvm::Constant *llvm_create_rtti(llvm::IRBuilder<> &builder, program_scope_t::ref program_scope, types::type_t::ref type);
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
		scope_t::ref scope,
		int iff,
		ptr<life_t> life,
		location_t location,
		bound_var_t::ref value,
        bool allow_maybe_check,
		llvm::BasicBlock *then_bb,
		llvm::BasicBlock *else_bb);

llvm::Type *llvm_deref_type(llvm::Type *llvm_pointer_type);
bound_var_t::ref llvm_start_function(
		llvm::IRBuilder<> &builder, 
		scope_t::ref scope,
		location_t location,
		const types::type_function_t::ref &function_type,
		std::string name);

bound_var_t::ref llvm_create_global_tag(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::ref tag_type,
		std::string tag,
		identifier::ref id);

// NOTE: the explain function is a tool to learn about LLVM types, it does not
// handle cyclic types, so it should only be used for debugging.
void explain(llvm::Type *llvm_type);

bound_var_t::ref maybe_load_from_pointer(
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		bound_var_t::ref var);
bound_var_t::ref llvm_stack_map_value(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        bound_var_t::ref value);
bool llvm_value_is_handle(llvm::Value *llvm_value);
bool llvm_value_is_pointer(llvm::Value *llvm_value);
llvm::StructType *llvm_find_struct(llvm::Type *llvm_type);
void llvm_generate_dead_return(llvm::IRBuilder<> &builder, scope_t::ref scope);
