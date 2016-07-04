#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "bound_var.h"
#include "scopes.h"

struct compiler;
struct status_t;

llvm::FunctionType *llvm_create_function_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const bound_type_t::refs &args,
		bound_type_t::ref return_value);

bound_var_t::ref create_callsite(
		status_t &status,
		llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		const ptr<const ast::item> &callsite,
		ptr<const bound_var_t> callee,
		atom name,
		const location &location,
		bound_var_t::refs values);

llvm::CallInst *llvm_create_call_inst(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ast::item &obj,
		ptr<const bound_var_t> callee,
		std::vector<llvm::Value *> llvm_values);

llvm::Value *llvm_create_bool(llvm::IRBuilder<> &builder, bool value);
llvm::Value *llvm_create_int(llvm::IRBuilder<> &builder, int64_t value);
llvm::Value *llvm_create_float(llvm::IRBuilder<> &builder, float value);
llvm::Value *llvm_create_global_string(llvm::IRBuilder<> &builder, std::string value);
llvm::Module *llvm_get_module(llvm::IRBuilder<> &builder);
llvm::Function *llvm_get_function(llvm::IRBuilder<> &builder);
std::string llvm_print_module(llvm::Module &module);
std::string llvm_print_value(llvm::Value &llvm_value);
std::string llvm_print_value_ptr(llvm::Value *llvm_value);
std::string llvm_print_type(llvm::Type &llvm_type);
llvm::AllocaInst *llvm_create_entry_block_alloca(llvm::Function *llvm_function, bound_type_t::ref type, atom var_name);
llvm::Value *llvm_resolve_alloca(llvm::IRBuilder<> &builder, llvm::Value *llvm_value);
llvm::Type *llvm_resolve_type(llvm::Value *llvm_value);
llvm::Type *llvm_create_tuple_type(llvm::IRBuilder<> &builder, program_scope_t::ref program_scope, atom name, const bound_type_t::refs &dimensions);
llvm::Type *llvm_create_sum_type(llvm::IRBuilder<> &builder, program_scope_t::ref program_scope, atom name);
llvm::Type *llvm_create_struct_type(llvm::IRBuilder<> &builder, atom name, const std::vector<llvm::Type*> &llvm_types);
llvm::Value *llvm_sizeof_type(llvm::IRBuilder<> &builder, llvm::Type *llvm_type);

void llvm_verify_function(status_t &status, llvm::Function *llvm_function);
void llvm_verify_module(status_t &status, llvm::Module &llvm_module);

/* llvm_wrap_type - wrap a normal data type in a managed var_t from the GC */
llvm::Type *llvm_wrap_type(llvm::IRBuilder<> &builder, program_scope_t::ref program_scope, atom data_name, llvm::Type *llvm_data_type);

void llvm_create_if_branch(
	   	llvm::IRBuilder<> &builder,
	   	llvm::Value *llvm_value,
	   	llvm::BasicBlock *then_bb,
	   	llvm::BasicBlock *else_bb);
llvm::Type *llvm_get_data_ctor_tag_basetype(llvm::IRBuilder<> &builder);
llvm::Type *llvm_deref_type(llvm::Type *llvm_pointer_type);
bound_var_t::ref llvm_start_function(
		status_t &status,
		llvm::IRBuilder<> &builder, 
		scope_t::ref scope,
		const ptr<const ast::item> &node,
		bound_type_t::refs args,
		bound_type_t::ref data_type,
		atom name);

bound_var_t::ref llvm_create_global_tag(
		llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		bound_type_t::ref tag_type,
		atom tag,
		identifier::ref id);
