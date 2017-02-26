#include "logger.h"
#include "utils.h"
#include "location.h"
#include "llvm_utils.h"
#include "type_checker.h"
#include "compiler.h"
#include "llvm_types.h"
#include "code_id.h"
#include "life.h"

llvm::Value *llvm_create_global_string(llvm::IRBuilder<> &builder, std::string value) {
	return builder.CreateGlobalStringPtr(value);
}

llvm::Constant *llvm_get_pointer_to_constant(
		llvm::IRBuilder<> &builder,
		llvm::Constant *llvm_constant)
{
	debug_above(9, log(log_info, "getting pointer to constant %s",
				llvm_print_value_ptr(llvm_constant).c_str()));
	return llvm::ConstantExpr::getGetElementPtr(
			nullptr, llvm_constant,
			llvm::ArrayRef<llvm::Value*>{builder.getInt32(0), builder.getInt32(0)});
}

llvm::Constant *llvm_create_global_string_constant(
		llvm::IRBuilder<> &builder,
	   	llvm::Module &M,
	   	std::string str)
{
	llvm::LLVMContext &Context = builder.getContext();
	llvm::Constant *StrConstant = llvm::ConstantDataArray::getString(Context, str);
	std::string name = std::string("__global_") + str;
	llvm::GlobalVariable *llvm_value = llvm_get_global(&M, name, StrConstant);
	return llvm_get_pointer_to_constant(builder, llvm_value);
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

llvm::Value *llvm_create_int32(llvm::IRBuilder<> &builder, int32_t value) {
	return builder.getInt32(value);
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

	assert(return_value->get_type()->ftv_count() == 0 && "return values should never be abstract");
	std::vector<llvm::Type *> llvm_type_args;

	for (auto &arg : args) {
		llvm_type_args.push_back(arg->get_llvm_specific_type());
	}

	auto p = llvm::FunctionType::get(
			return_value->get_llvm_specific_type(),
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
		life_t::ref life,
		const bound_var_t::ref function,
		atom name,
		const location_t &location,
		bound_var_t::refs arguments)
{
	if (!!status) {
		debug_above(5, log(log_info, "create_callsite is assuming %s is compatible with %s\n%s",
					function->get_type()->str().c_str(),
					str(arguments).c_str(),
					llvm_print_function(static_cast<llvm::Function *>(function->llvm_value)).c_str()));
		debug_above(5, log(log_info, "calling function %s",
					llvm_print_type(function->llvm_value->getType()).c_str()));

		/* downcast the arguments as necessary to var_t * */
		types::type_function_t::ref function_type = dyncast<const types::type_function_t>(function->get_type());
		if (function_type != nullptr) {
			llvm::CallInst *llvm_call_inst = llvm_create_call_inst(
					status, builder, location, function, get_llvm_values(arguments),
					life);

			if (!!status) {
				bound_type_t::ref return_type = get_function_return_type(status,
						builder, scope, function->type);

				return bound_var_t::create(INTERNAL_LOC(), name, return_type, llvm_call_inst,
						make_type_id_code_id(INTERNAL_LOC(), name), false/*is_lhs*/);
			}
		} else {
			user_error(status, location,
					"tried to create_callsite for %s, but it's not a function?",
					function->str().c_str());
		}
	}

	assert(!status);
	return nullptr;
}

llvm::CallInst *llvm_create_call_inst(
		status_t &status,
		llvm::IRBuilder<> &builder,
		location_t location,
		ptr<const bound_var_t> callee,
		std::vector<llvm::Value *> llvm_values,
		life_t::ref life)
{
	assert(callee != nullptr);
	assert(callee->llvm_value != nullptr);

	llvm::Value *llvm_value = llvm_resolve_alloca(builder, callee->llvm_value);

	llvm::Function *llvm_callee_fn = llvm::dyn_cast<llvm::Function>(llvm_value);

	/* get the function we want to call */
	if (!llvm_callee_fn) {
		user_error(status, location, "could not find function %s",
				callee->str().c_str());
		return nullptr;
	}

	/* get the current module we're inserting code into */
	llvm::Module *llvm_module = llvm_get_module(builder);

	debug_above(3, log(log_info, "looking for function in LLVM " c_id("%s") " with type %s",
				llvm_callee_fn->getName().str().c_str(),
				llvm_print_type(llvm_callee_fn->getFunctionType()).c_str()));

	/* before we can call a function, we must make sure it either exists in
	 * this module, or a declaration exists */
	auto llvm_func_decl = llvm::cast<llvm::Function>(
			llvm_module->getOrInsertFunction(
				llvm_callee_fn->getName(),
				llvm_callee_fn->getFunctionType(),
				llvm_callee_fn->getAttributes()));

	auto llvm_function_type = llvm::dyn_cast<llvm::FunctionType>(llvm_func_decl->getType()->getElementType());
	assert(llvm_function_type != nullptr);
	debug_above(3, log(log_info, "creating call to %s",
				llvm_print_type(llvm_function_type).c_str()));

	auto param_iter = llvm_function_type->param_begin();
	std::vector<llvm::Value *> llvm_args;

	/* make one last pass over the parameters before we make this call */
	int index = 0;
	for (auto &llvm_value : llvm_values) {
		llvm::Value *llvm_arg = llvm_maybe_pointer_cast(builder, llvm_value, *param_iter);
		llvm_arg->setName(string_format("call.arg.%d", index));

		llvm_args.push_back(llvm_arg);

		++param_iter;
		++index;
	}
	llvm::ArrayRef<llvm::Value *> llvm_args_array(llvm_args);

	debug_above(3, log(log_info, "creating call to " c_id("%s") " %s with [%s]",
				llvm_func_decl->getName().str().c_str(),
				llvm_print_type(llvm_function_type).c_str(),
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

std::string llvm_print_function(llvm::Function *llvm_function) {
	std::stringstream ss;
	llvm::raw_os_ostream os(ss);
	llvm_function->print(os, nullptr /*AssemblyAnnotationWriter*/);
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

std::string llvm_print_type(llvm::Type *llvm_type) {
	std::stringstream ss;
	llvm::raw_os_ostream os(ss);
	ss << C_IR;
	if (llvm_type->isPointerTy()) {
		llvm_type = llvm::cast<llvm::PointerType>(llvm_type)->getElementType();
		ss << " {";
		llvm_type->print(os);
		os.flush();
	   	ss << "}*";
	} else {
		llvm_type->print(os);
		os.flush();
	}
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
	return builder.CreateAlloca(type->get_llvm_specific_type(), nullptr, var_name.c_str());
}

void llvm_create_if_branch(
	   	llvm::IRBuilder<> &builder,
	   	llvm::Value *llvm_value,
	   	llvm::BasicBlock *then_bb,
	   	llvm::BasicBlock *else_bb)
{
	llvm::Type *llvm_type = llvm_value->getType();

	if (llvm_type->isPointerTy()) {
		/* automatically check pointers against null */
		llvm::Constant *null_value = llvm::Constant::getNullValue(llvm_type);
		llvm_value = builder.CreateICmpNE(llvm_value, null_value);
		llvm_type = llvm_value->getType();
	}

	if (!llvm_type->isIntegerTy(1)) {
		llvm::Constant *zero = llvm::ConstantInt::get(llvm_type, 0);
		llvm_value = builder.CreateICmpNE(llvm_value, zero);
	}

	assert(llvm_value->getType()->isIntegerTy(1));
	builder.CreateCondBr(llvm_value, then_bb, else_bb);
}

llvm::StructType *llvm_create_struct_type(
		llvm::IRBuilder<> &builder,
		atom name,
		const std::vector<llvm::Type*> &llvm_types)
{
	llvm::ArrayRef<llvm::Type*> llvm_dims{llvm_types};

	auto llvm_struct_type = llvm::StructType::create(builder.getContext(), llvm_dims);

	/* give the struct a helpful name internally */
	llvm_struct_type->setName(name.str());

	debug_above(3, log(log_info, "created struct type " c_id("%s") " %s",
				name.c_str(),
				llvm_print_type(llvm_struct_type).c_str()));

	return llvm_struct_type;
}

llvm::StructType *llvm_create_struct_type(
		llvm::IRBuilder<> &builder,
		atom name,
		const bound_type_t::refs &dimensions) 
{
	std::vector<llvm::Type*> llvm_types;

	/* now add all the dimensions of the tuple */
	for (auto &dimension : dimensions) {
		llvm_types.push_back(dimension->get_llvm_specific_type());
	}

	llvm::StructType *llvm_tuple_type = llvm_create_struct_type(builder, name, llvm_types);

	/* the actual llvm return type is a managed variable */
	return llvm_tuple_type;
}

llvm::Type *llvm_create_sum_type(
		llvm::IRBuilder<> &builder,
		program_scope_t::ref program_scope,
		atom name)
{
	llvm::StructType *llvm_sum_type = llvm_create_struct_type(builder, name,
			std::vector<llvm::Type*>{});

	/* the actual llvm return type is a managed variable */
	return llvm_wrap_type(builder, program_scope, name, llvm_sum_type);
}

llvm::Type *llvm_wrap_type(
		llvm::IRBuilder<> &builder,
		program_scope_t::ref program_scope,
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
	llvm::Type *llvm_var_type = var_type->get_llvm_type();

	llvm::ArrayRef<llvm::Type*> llvm_dims{llvm_var_type, llvm_data_type};
	auto llvm_struct_type = llvm::StructType::create(builder.getContext(), llvm_dims);

	/* give the struct a helpful name internally */
	llvm_struct_type->setName(data_name.str());

	/* we'll be referring to pointers to these variable structures */
	return llvm_struct_type;
}

void llvm_verify_function(status_t &status, llvm::Function *llvm_function) {
	std::stringstream ss;
	llvm::raw_os_ostream os(ss);
	if (llvm::verifyFunction(*llvm_function, &os)) {
		os.flush();
		ss << llvm_print_function(llvm_function);
		user_error(status, location_t{}, "LLVM function verification failed: %s", ss.str().c_str());
	}
}

void llvm_verify_module(status_t &status, llvm::Module &llvm_module) {
	std::stringstream ss;
	llvm::raw_os_ostream os(ss);
	if (llvm::verifyModule(llvm_module, &os)) {
		os.flush();
		user_error(status, location_t{}, "module %s: failed verification. %s\nModule listing:\n%s",
				llvm_module.getName().str().c_str(), ss.str().c_str(),
				llvm_print_module(llvm_module).c_str());
		
	}
}

llvm::Value *llvm_sizeof_type(llvm::IRBuilder<> &builder, llvm::Type *llvm_type) {
	llvm::StructType *llvm_struct_type = llvm::dyn_cast<llvm::StructType>(llvm_type);
	if (llvm_struct_type != nullptr) {
		if (llvm_struct_type->isOpaque()) {
			debug_above(1, log("llvm_struct_type is opaque when we're trying to get its size: %s",
						llvm_print_type(llvm_struct_type).c_str()));
			assert(false);
		}
		assert(llvm_struct_type->elements().size() != 0);
	}

	llvm::Constant *alloc_size_const = llvm::ConstantExpr::getSizeOf(llvm_type);
	llvm::Value *size_value = llvm::ConstantExpr::getTruncOrBitCast(alloc_size_const, builder.getInt64Ty());
	debug_above(3, log(log_info, "size of %s is: %s", llvm_print_type(llvm_type).c_str(),
				llvm_print_value(*size_value).c_str()));
	return size_value;

}

llvm::Type *llvm_deref_type(llvm::Type *llvm_type) {
	if (llvm_type->isPointerTy()) {
		return llvm::cast<llvm::PointerType>(llvm_type)->getElementType();
	} else {
		return llvm_type;
	}
}

bound_var_t::ref llvm_start_function(status_t &status,
		llvm::IRBuilder<> &builder, 
		scope_t::ref scope,
		const ast::item_t::ref &node,
		types::type_t::ref type_fn_context,
		bound_type_t::refs args,
		bound_type_t::ref data_type,
		atom name)
{
	if (!!status) {
		/* get the llvm function type for the data ctor */
		llvm::FunctionType *llvm_ctor_fn_type = llvm_create_function_type(
				status, builder, args, data_type);

		if (!!status) {
			/* create the bound type for the ctor function */
			auto function_type = bound_type_t::create(
						get_function_type(type_fn_context, args, data_type),
						node->token.location,
						llvm_ctor_fn_type);
			/* now let's generate our actual data ctor fn */
			auto llvm_function = llvm::Function::Create(
					(llvm::FunctionType *)llvm_ctor_fn_type,
					llvm::Function::ExternalLinkage, name.str(),
					scope->get_llvm_module());

			/* create the actual bound variable for the fn */
			bound_var_t::ref function = bound_var_t::create(
					INTERNAL_LOC(), name,
					function_type, llvm_function, make_code_id(node->token),
					false/*is_lhs*/);

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

void check_struct_initialization(
		llvm::ArrayRef<llvm::Constant*> llvm_struct_initialization,
	   	llvm::StructType *llvm_struct_type)
{
	for (unsigned i = 0, e = llvm_struct_initialization.size(); i != e; ++i) {
		if (llvm_struct_initialization[i]->getType() == llvm_struct_type->getElementType(i)) {
		   continue;
		} else {
			debug_above(7, log(log_error, "llvm_struct_initialization[%d] mismatch is %s should be %s",
						i,
					   	llvm_print_value(*llvm_struct_initialization[i]).c_str(),
						llvm_print_type(llvm_struct_type->getElementType(i)).c_str()));
			assert(false);
		}
	}
}

llvm::GlobalVariable *llvm_get_global(
		llvm::Module *llvm_module,
		std::string name,
		llvm::Constant *llvm_constant)
{
	auto llvm_global_variable = new llvm::GlobalVariable(*llvm_module,
			llvm_constant->getType(),
			true /* isConstant */, llvm::GlobalValue::PrivateLinkage,
			llvm_constant, name, nullptr,
			llvm::GlobalVariable::NotThreadLocal);

	llvm_global_variable->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
	return llvm_global_variable;
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
	 * @__tag_type_info_Example = global %struct.type_info_t { i32 42, i16 -1, i16* null, i8* getelementptr inbounds ([5 x i8], [5 x i8]* @.str, i32 0, i32 0), i16 0 }, align 8
	 * @__tag_Example = global %struct.tag_t { %struct.type_info_t* @__tag_type_info_Example }, align 8
	 * @Example = global %struct.var_t* bitcast (%struct.tag_t* @__tag_Example to %struct.var_t*), align 8 */

	bound_type_t::ref var_ref_type = program_scope->get_bound_type({"__var_ref"});
	bound_type_t::ref tag_struct_type = program_scope->get_bound_type({"__tag_var"});

	llvm::Type *llvm_var_ref_type = var_ref_type->get_llvm_type();
	llvm::StructType *llvm_tag_type = llvm::dyn_cast<llvm::StructType>(tag_struct_type->get_llvm_type());
	debug_above(10, log(log_info, "var_ref_type is %s", llvm_print_type(var_ref_type->get_llvm_type()).c_str()));
	debug_above(10, log(log_info, "tag_struct_type is %s", llvm_print_type(tag_struct_type->get_llvm_type()).c_str()));
	assert(llvm_tag_type != nullptr);

	llvm::Module *llvm_module = scope->get_llvm_module();
	assert(llvm_module != nullptr);

	llvm::Constant *llvm_name = llvm_create_global_string_constant(builder, *llvm_module, tag.str());
	debug_above(10, log(log_info, "llvm_name is %s", llvm_print_value(*llvm_name).c_str()));

	llvm::StructType *llvm_type_info_type = llvm::cast<llvm::StructType>(
			program_scope->get_bound_type({"__type_info"})->get_llvm_type());

	std::vector<llvm::Constant *> llvm_tag_data({
			/* type_id - the actual type "tag" */
			(llvm::Constant *)llvm_create_int32(builder, tag.iatom),

			/* the number of contained references */
			builder.getInt16(-1),

			/* there are no managed references in a tag */
			llvm::Constant::getNullValue(builder.getInt16Ty()->getPointerTo()),

			/* name - for debugging */
			llvm_name,

			/* size - should always be zero since the type_id is part of this var_t
			 * as builtin type info. */
			builder.getInt16(0)});

	llvm::ArrayRef<llvm::Constant*> llvm_tag_initializer{llvm_tag_data};

	check_struct_initialization(llvm_tag_initializer, llvm_type_info_type);

	llvm::GlobalVariable *llvm_type_info = llvm_get_global(
			llvm_module, std::string("__tag_type_info_") + tag.str(),
			llvm::ConstantStruct::get(llvm_type_info_type,
				llvm_tag_initializer));

	llvm::GlobalVariable *llvm_tag_constant = llvm_get_global(llvm_module,
			std::string("__tag_") + tag.str(),
			llvm::ConstantStruct::get(llvm_tag_type,
				llvm_type_info, nullptr));

	debug_above(10, log(log_info, "getBitCast(%s, %s)",
				llvm_print_value(*llvm_tag_constant).c_str(),
				llvm_print_type(llvm_var_ref_type).c_str()));
	llvm::Constant *llvm_tag_value = llvm::ConstantExpr::getPointerCast(
			llvm_tag_constant, llvm_var_ref_type);

	return bound_var_t::create(INTERNAL_LOC(), tag, tag_type, llvm_tag_value,
			id, false/*is_lhs*/);
}

llvm::Value *llvm_maybe_pointer_cast(
		llvm::IRBuilder<> &builder,
	   	llvm::Value *llvm_value,
	   	llvm::Type *llvm_type)
{
	llvm_value = llvm_resolve_alloca(builder, llvm_value);

	if (llvm_type->isPointerTy()) {
		debug_above(6, log("attempting to cast %s to a %s",
					llvm_print_value_ptr(llvm_value).c_str(),
					llvm_print_type(llvm_type).c_str()));
		assert(llvm_value->getType()->isPointerTy());

		if (llvm_type != llvm_value->getType()) {
			return builder.CreatePointerBitCastOrAddrSpaceCast(
					llvm_value, 
					llvm_type);
		}
	}

	return llvm_value;
}

llvm::Value *llvm_maybe_pointer_cast(
		llvm::IRBuilder<> &builder,
	   	llvm::Value *llvm_value,
	   	const bound_type_t::ref &bound_type)
{
	return llvm_maybe_pointer_cast(builder, llvm_value, bound_type->get_llvm_specific_type());
}

void explain(llvm::Type *llvm_type) {
	indent_logger indent(6,
		string_format("explain %s",
			llvm_print_type(llvm_type).c_str()));

	if (auto llvm_struct_type = llvm::dyn_cast<llvm::StructType>(llvm_type)) {
		for (auto element: llvm_struct_type->elements()) {
			explain(element);
		}
	} else if (auto lp = llvm::dyn_cast<llvm::PointerType>(llvm_type)) {
		explain(lp->getElementType());
	}
}

