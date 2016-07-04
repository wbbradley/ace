#include "utils.h"
#include "llvm_utils.h"
#include "type_checker.h"
#include "compiler.h"
#include "llvm_types.h"
#include "code_id.h"

llvm::Value *llvm_create_global_string(llvm::IRBuilder<> &builder, std::string value) {
	return builder.CreateGlobalStringPtr(value);
}

llvm::Value *llvm_create_bool(llvm::IRBuilder<> &builder, bool value) {
	if (value) {
		return builder.getTrue();
	} else {
		return builder.getFalse();
	}
}

llvm::Value *llvm_create_int(llvm::IRBuilder<> &builder, int64_t value) {
	return builder.getInt64(value);
}

llvm::Value *llvm_create_int16(llvm::IRBuilder<> &builder, int16_t value) {
	return builder.getInt16(value);
}

llvm::Value *llvm_create_float(llvm::IRBuilder<> &builder, float value) {
	return llvm::ConstantFP::get(builder.getContext(), llvm::APFloat(value));
}

llvm::FunctionType *llvm_create_function_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const bound_type_t::refs &args,
		bound_type_t::ref return_value)
{
	debug_above(4, log(log_info, "creating an LLVM function type from (%s %s)",
		::str(args).c_str(),
		return_value->str().c_str()));

	std::vector<llvm::Type *> llvm_type_args;

	for (auto &arg : args) {
		llvm_type_args.push_back(arg->llvm_type);
	}

	auto p = llvm::FunctionType::get(
			return_value->llvm_type,
			llvm::ArrayRef<llvm::Type*>(llvm_type_args),
			false /*isVarArg*/);
	assert(p->isFunctionTy());
	return p;
}

llvm::Type *llvm_resolve_type(llvm::Value *llvm_value) {
	if (llvm::AllocaInst *alloca = llvm::dyn_cast<llvm::AllocaInst>(llvm_value)) {
		return alloca->getAllocatedType();
	} else {
		return llvm_value->getType();
	}
}

llvm::Value *llvm_resolve_alloca(llvm::IRBuilder<> &builder, llvm::Value *llvm_value) {
	if (llvm::AllocaInst *alloca = llvm::dyn_cast<llvm::AllocaInst>(llvm_value)) {
		return builder.CreateLoad(alloca);
	} else {
		return llvm_value;
	}
}

bound_var_t::ref create_callsite(
		status_t &status,
		llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		const ptr<const ast::item> &callsite,
		const bound_var_t::ref function,
		atom name,
		const location &location,
		bound_var_t::refs arguments)
{
	if (!!status) {
		debug_above(5, log(log_info, "create_callsite is assuming %s is compatible with %s",
					function->get_term()->str().c_str(),
					get_args_term(arguments)->str().c_str()));

		llvm::CallInst *llvm_call_inst = llvm_create_call_inst(
				status, builder, *callsite, function, get_llvm_values(arguments));

		if (!!status) {
			bound_type_t::ref return_type = get_function_return_type(status,
					builder, *callsite, scope, function->type);

			return bound_var_t::create(INTERNAL_LOC(), name, return_type, llvm_call_inst,
					make_code_id(callsite->token));
		}
	}
	
	assert(!status);
	return nullptr;
}

llvm::CallInst *llvm_create_call_inst(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ast::item &obj,
		ptr<const bound_var_t> callee,
		std::vector<llvm::Value *> llvm_values)
{
	assert(callee != nullptr);
	assert(callee->llvm_value != nullptr);

	llvm::Value *llvm_value = llvm_resolve_alloca(builder, callee->llvm_value);

	llvm::Function *llvm_callee_fn = llvm::dyn_cast<llvm::Function>(llvm_value);

	/* get the function we want to call */
	if (!llvm_callee_fn) {
		panic("somehow we did not get a function from our llvm_value");
	}

	/* get the current module we're inserting code into */
	llvm::Module *llvm_module = llvm_get_module(builder);

	/* before we can call a function, we must make sure it either exists in
	 * this module, or a declaration exists */
	auto llvm_func_decl = llvm::cast<llvm::Function>(
			llvm_module->getOrInsertFunction(
				llvm_callee_fn->getName(),
				llvm_callee_fn->getFunctionType()));

	std::vector<llvm::Value *> llvm_args;
	for (auto &llvm_value : llvm_values) {
		if (llvm_value != nullptr) {
			llvm_args.push_back(llvm_resolve_alloca(builder, llvm_value));
		} else {
			panic(string_format("found a null llvm_value while creating call instruction: %s",
						llvm_print_value_ptr(llvm_value).c_str()));
		}
	}
	llvm::ArrayRef<llvm::Value *> llvm_args_array(llvm_args);

	debug_above(3, log(log_info, "creating call to %s %s with [%s]",
				llvm_func_decl->getName().str().c_str(),
				llvm_print_type(*llvm_func_decl->getType()).c_str(),
				join_with(llvm_args, ", ", llvm_print_value_ptr).c_str()));

	return builder.CreateCall(llvm_func_decl, llvm_args_array);
}

llvm::Module *llvm_get_module(llvm::IRBuilder<> &builder) {
	return builder.GetInsertBlock()->getParent()->getParent();
}

llvm::Function *llvm_get_function(llvm::IRBuilder<> &builder) {
	return builder.GetInsertBlock()->getParent();
}

std::string llvm_print_module(llvm::Module &llvm_module) {
	std::stringstream ss;
	llvm::raw_os_ostream os(ss);
	llvm_module.print(os, nullptr /*AssemblyAnnotationWriter*/);
	os.flush();
	return ss.str();
}

std::string llvm_print_value_ptr(llvm::Value *llvm_value) {
	return llvm_print_value(*llvm_value);
}

std::string llvm_print_value(llvm::Value &llvm_value) {
	std::stringstream ss;
	llvm::raw_os_ostream os(ss);
	llvm_value.print(os);
	os.flush();
	ss << " : " << C_IR;
	llvm_value.getType()->print(os);
	os.flush();
	ss << C_RESET;
	return ss.str();
}

std::string llvm_print_type(llvm::Type &llvm_type) {
	std::stringstream ss;
	llvm::raw_os_ostream os(ss);
	ss << C_IR;
	llvm_type.print(os);
	os.flush();
	ss << C_RESET;
	return ss.str();
}

llvm::AllocaInst *llvm_create_entry_block_alloca(
		llvm::Function *llvm_function,
	   	bound_type_t::ref type,
	   	atom var_name)
{
	/* we'll need to place the alloca instance in the entry block, so let's
	 * make a builder that points there */
	llvm::IRBuilder<> builder(
			&llvm_function->getEntryBlock(),
		   	llvm_function->getEntryBlock().begin());

	/* create the local variable */
	return builder.CreateAlloca(type->llvm_type, nullptr, var_name.c_str());
}

void llvm_create_if_branch(
	   	llvm::IRBuilder<> &builder,
	   	llvm::Value *llvm_value,
	   	llvm::BasicBlock *then_bb,
	   	llvm::BasicBlock *else_bb)
{
	llvm::Type *llvm_type = llvm_value->getType();
	assert(llvm_value->getType()->isIntegerTy());

	if (!llvm_type->isIntegerTy(1)) {
		llvm::Constant *zero = llvm::ConstantInt::get(llvm_type, 0);
		llvm_value = builder.CreateICmpNE(llvm_value, zero);
	}

	assert(llvm_value->getType()->isIntegerTy(1));
	builder.CreateCondBr(llvm_value, then_bb, else_bb);
}

llvm::Type *llvm_get_data_ctor_tag_basetype(llvm::IRBuilder<> &builder) {
	return builder.getInt64Ty();
}

llvm::Type *llvm_create_struct_type(
		llvm::IRBuilder<> &builder,
		atom name,
		const std::vector<llvm::Type*> &llvm_types)
{
	llvm::ArrayRef<llvm::Type*> llvm_dims{llvm_types};

	auto llvm_struct_type = llvm::StructType::create(builder.getContext(), llvm_dims);

	/* give the struct a helpful name internally */
	llvm_struct_type->setName(name.str());

	debug_above(3, log(log_info, "created " c_ir("%s") " LLVM struct type %s",
				name.c_str(),
				llvm_print_type(*llvm_struct_type).c_str()));

	return llvm_struct_type;
}

llvm::Type *llvm_create_tuple_type(
		llvm::IRBuilder<> &builder,
		program_scope_t::ref program_scope,
		atom name,
		const bound_type_t::refs &dimensions) 
{
	std::vector<llvm::Type*> llvm_types;

	/* now add all the dimensions of the tuple */
	for (auto &dimension : dimensions) {
		llvm_types.push_back(dimension->llvm_type);
	}

	llvm::Type *llvm_tuple_type = llvm_create_struct_type(builder, name, llvm_types);

	/* the actual llvm return type is a managed variable */
	return llvm_wrap_type(builder, program_scope, name, llvm_tuple_type);
}

llvm::Type *llvm_create_sum_type(
		llvm::IRBuilder<> &builder,
		program_scope_t::ref program_scope,
		atom name)
{
	llvm::Type *llvm_sum_type = llvm_create_struct_type(builder, name, {});

	/* the actual llvm return type is a managed variable */
	return llvm_wrap_type(builder, program_scope, name, llvm_sum_type);
}

llvm::Type *llvm_wrap_type(
		llvm::IRBuilder<> &builder,
		program_scope_t::ref
		program_scope,
		atom data_name,
		llvm::Type *llvm_data_type)
{
	/* take something like this:
	 *
	 * typedef WHATEVER data_type_t;
	 *
	 * and wrap it like this:
	 *
	 * struct wrapped_data_type_t {
	 *   var_t mgmt;
	 *   data_type_t data;
	 * };
	 *
	 * This is to allow this type to be managed by the GC.
	 */
	bound_type_t::ref var_type = program_scope->get_bound_type({"__var"});
	llvm::Type *llvm_var_type = var_type->llvm_type;

	llvm::ArrayRef<llvm::Type*> llvm_dims{llvm_var_type, llvm_data_type};
	auto llvm_struct_type = llvm::StructType::create(builder.getContext(), llvm_dims);

	/* give the struct a helpful name internally */
	llvm_struct_type->setName(std::string("__var_ref_") + data_name.str());

	/* we'll be referring to pointers to these variable structures */
	return llvm_struct_type->getPointerTo();
}

void llvm_verify_function(status_t &status, llvm::Function *llvm_function) {
	std::stringstream ss;
	llvm::raw_os_ostream os(ss);
	if (llvm::verifyFunction(*llvm_function, &os)) {
		os.flush();
		user_error(status, location{}, "LLVM function verification failed: %s", ss.str().c_str());
	}
}

void llvm_verify_module(status_t &status, llvm::Module &llvm_module) {
	std::stringstream ss;
	llvm::raw_os_ostream os(ss);
	if (llvm::verifyModule(llvm_module, &os)) {
		os.flush();
		user_error(status, location{}, "module %s: failed verification. %s\nModule listing:\n%s",
				llvm_module.getName().str().c_str(), ss.str().c_str(),
				llvm_print_module(llvm_module).c_str());
		
	}
}

llvm::Value *llvm_sizeof_type(llvm::IRBuilder<> &builder, llvm::Type *llvm_type) {
	llvm::Constant *alloc_size_const = llvm::ConstantExpr::getSizeOf(llvm_type);
	llvm::Value *size_value = llvm::ConstantExpr::getTruncOrBitCast(alloc_size_const, builder.getInt64Ty());
	debug_above(3, log(log_info, "size of %s is: %s", llvm_print_type(*llvm_type).c_str(),
				llvm_print_value(*size_value).c_str()));
	return size_value;

}

llvm::Type *llvm_deref_type(llvm::Type *llvm_pointer_type) {
	return llvm::cast<llvm::PointerType>(llvm_pointer_type)->getElementType();
}

bound_var_t::ref llvm_start_function(status_t &status,
		llvm::IRBuilder<> &builder, 
		scope_t::ref scope,
		const ast::item::ref &node,
		bound_type_t::refs args,
		bound_type_t::ref data_type,
		atom name)
{
	if (!!status) {
		/* get the llvm function type for the data ctor */
		assert(false /* try re-using create_bound_type */);
		llvm::FunctionType *llvm_ctor_fn_type = llvm_create_function_type(
				status, builder, args, data_type);

		if (!!status) {
			/* create the bound type for the ctor function */
			auto function_type =
				bound_type_t::create(
						get_function_type(args, data_type),
						node->token.location,
						llvm_ctor_fn_type);
			/* now let's generate our actual data ctor fn */
			auto llvm_function =
				llvm::Function::Create(
						(llvm::FunctionType *)llvm_ctor_fn_type,
						llvm::Function::ExternalLinkage, name.str(),
						scope->get_llvm_module());

			/* create the actual bound variable for the data ctor fn */
			bound_var_t::ref function = bound_var_t::create(
					INTERNAL_LOC(), name,
					function_type, llvm_function, make_code_id(node->token));

			/* start emitting code into the new function. caller should have an
			 * insert point guard */
			llvm::BasicBlock *llvm_block = llvm::BasicBlock::Create(builder.getContext(),
					"entry", llvm_function);
			builder.SetInsertPoint(llvm_block);

			return function;
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref llvm_create_global_tag(
		llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		bound_type_t::ref tag_type,
		atom tag,
		identifier::ref id)
{
	auto program_scope = scope->get_program_scope();

	/* For a tag called "Example" with a type_id of 42, the LLIR should look
	 * like this:
	 *
	 * @.str = private unnamed_addr constant [5 x i8] c"Example\00", align 1
	 * @__tag_Example = global %struct.tag_t { i64 0, i16 0, i8* getelementptr inbounds ([5 x i8], [5 x i8]* @.str, i32 0, i32 0), i32 42 }, align 8
	 * @Example = global %struct.var_t* bitcast (%struct.tag_t* @__tag_Example to %struct.var_t*), align 8 */

	bound_type_t::ref var_ref_type = program_scope->get_bound_type({"__var_ref"});
	bound_type_t::ref tag_struct_type = program_scope->get_bound_type({"__tag_var"});

	llvm::Type *llvm_var_ref_type = var_ref_type->llvm_type;
	llvm::StructType *llvm_tag_type = llvm::dyn_cast<llvm::StructType>(tag_struct_type->llvm_type);
	debug_above(10, log(log_info, "var_ref_type is %s", llvm_print_type(*var_ref_type->llvm_type).c_str()));
	debug_above(10, log(log_info, "tag_struct_type is %s", llvm_print_type(*tag_struct_type->llvm_type).c_str()));
	assert(llvm_tag_type != nullptr);

	std::vector<llvm::Constant *> llvm_tag_data({
		/* GC version - should always be zero since this is a global and must
		 * never be collected */
		(llvm::Constant *)llvm_create_int(builder, 0),

		/* size - should always be zero since the type_id is part of this var_t
		 * as builtin type info. */
		(llvm::Constant *)llvm_create_int16(builder, 0),

		/* name - for debugging */
		(llvm::Constant *)llvm_create_global_string(builder, tag.str()),

		/* type_id - the actual type "tag" */
		(llvm::Constant *)llvm_create_int(builder, tag.iatom),
	});

	llvm::ArrayRef<llvm::Constant*> llvm_tag_initializer{llvm_tag_data};

	llvm::Constant *llvm_tag_global = llvm::ConstantStruct::get(llvm_tag_type,
			llvm_tag_initializer);

	llvm::Constant *llvm_tag_value = llvm::ConstantExpr::getBitCast(
			llvm_tag_global, llvm_var_ref_type);

	return bound_var_t::create(INTERNAL_LOC(), tag, tag_struct_type, llvm_tag_value,
			id);
}
