#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "bound_var.h"
#include "scopes.h"
#include "life.h"

#define DTOR_FN_INDEX 4
#define MARK_FN_INDEX 5

extern const char *GC_STRATEGY;

struct compiler_t;
struct status_t;
struct life_t;

llvm::FunctionType *llvm_create_function_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const bound_type_t::refs &args,
		bound_type_t::ref return_value);

bound_var_t::ref create_callsite(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		ptr<life_t> life,
		ptr<const bound_var_t> callee,
		atom name,
		const location_t &location,
		bound_var_t::refs values);

llvm::CallInst *llvm_create_call_inst(
		status_t &status,
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
llvm::Value *llvm_create_int(llvm::IRBuilder<> &builder, int64_t value);
llvm::Value *llvm_create_int32(llvm::IRBuilder<> &builder, int32_t value);
llvm::Value *llvm_create_double(llvm::IRBuilder<> &builder, double value);
llvm::GlobalVariable *llvm_get_global(llvm::Module *llvm_module, std::string name, llvm::Constant *llvm_constant, bool is_constant);
llvm::Value *llvm_create_global_string(llvm::IRBuilder<> &builder, std::string value);
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
llvm::AllocaInst *llvm_create_entry_block_alloca(llvm::Function *llvm_function, bound_type_t::ref type, atom var_name);
llvm::AllocaInst *llvm_call_gcroot(llvm::Function *llvm_function, bound_type_t::ref type, atom var_name);

llvm::Value *_llvm_resolve_alloca(llvm::IRBuilder<> &builder, llvm::Value *llvm_value);
llvm::Type *llvm_resolve_type(llvm::Value *llvm_value);
llvm::StructType *llvm_create_struct_type(llvm::IRBuilder<> &builder, atom name, const bound_type_t::refs &dimensions);
llvm::StructType *llvm_create_struct_type(llvm::IRBuilder<> &builder, atom name, const std::vector<llvm::Type*> &llvm_types);
llvm::Constant *llvm_sizeof_type(llvm::IRBuilder<> &builder, llvm::Type *llvm_type);
llvm::Value *llvm_maybe_pointer_cast(llvm::IRBuilder<> &builder, llvm::Value *llvm_value, llvm::Type *llvm_type);
llvm::Value *llvm_maybe_pointer_cast(llvm::IRBuilder<> &builder, llvm::Value *llvm_value, const bound_type_t::ref &bound_type);
llvm::Constant *llvm_get_pointer_to_constant(llvm::IRBuilder<> &builder, llvm::Constant *llvm_constant);
void check_struct_initialization(
		llvm::ArrayRef<llvm::Constant*> llvm_struct_initialization,
		llvm::StructType *llvm_struct_type);

void llvm_verify_function(status_t &status, llvm::Function *llvm_function);
void llvm_verify_module(status_t &status, llvm::Module &llvm_module);

/* llvm_wrap_type - wrap a normal data type in a managed var_t from the GC */
llvm::Type *llvm_wrap_type(status_t &status, llvm::IRBuilder<> &builder, program_scope_t::ref program_scope, atom data_name, llvm::Type *llvm_data_type);

/* flags for llvm_create_if_branch that tell it whether to invoke release_vars
 * for either branch */

struct life_t;

#define IFF_THEN 1
#define IFF_ELSE 2
#define IFF_BOTH (IFF_ELSE | IFF_THEN)

void llvm_create_if_branch(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		int iff,
		ptr<life_t> life,
		llvm::Value *llvm_value,
		llvm::BasicBlock *then_bb,
		llvm::BasicBlock *else_bb);

llvm::Type *llvm_deref_type(llvm::Type *llvm_pointer_type);
bound_var_t::ref llvm_start_function(
		status_t &status,
		llvm::IRBuilder<> &builder, 
		scope_t::ref scope,
		const ptr<const ast::item_t> &node,
		bound_type_t::refs args,
		bound_type_t::ref data_type,
		atom name);

bound_var_t::ref llvm_create_global_tag(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::ref tag_type,
		atom tag,
		identifier::ref id);

// NOTE: the explain function is a tool to learn about LLVM types, it does not
// handle cyclic types, so it should only be used for debugging.
void explain(llvm::Type *llvm_type);

bound_var_t::ref maybe_load_from_pointer(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		bound_var_t::ref var);
bound_var_t::ref llvm_stack_map_value(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        bound_var_t::ref value);
bool llvm_value_is_handle(llvm::Value *llvm_value);
bool llvm_value_is_pointer(llvm::Value *llvm_value);
bound_var_t::ref get_nil_constant(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		types::type_t::ref type);
