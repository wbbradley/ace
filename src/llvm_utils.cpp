#include <iostream>
#include "atom.h"
#include "logger.h"
#include "utils.h"
#include "location.h"
#include "llvm_utils.h"
#include "type_checker.h"
#include "compiler.h"
#include "llvm_types.h"
#include "code_id.h"
#include "life.h"
#include "type_kind.h"
#include "coercions.h"
#include "unification.h"

const char *GC_STRATEGY = "zion";


llvm::Value *llvm_create_global_string(llvm::IRBuilder<> &builder, std::string value) {
	return builder.CreateGlobalStringPtr(value);
}

llvm::Constant *llvm_get_pointer_to_constant(
		llvm::IRBuilder<> &builder,
		llvm::Constant *llvm_constant)
{
	assert(llvm::dyn_cast<llvm::PointerType>(llvm_constant->getType()) != nullptr);

	debug_above(9, log(log_info, "getting pointer to constant %s",
				llvm_print(llvm_constant).c_str()));

	std::vector<llvm::Constant *> gep_indices = {
		builder.getInt32(0),
		builder.getInt32(0)
	};

	return llvm::ConstantExpr::getInBoundsGetElementPtr(nullptr, llvm_constant, gep_indices);
}

llvm::Constant *llvm_create_global_string_constant(
		llvm::IRBuilder<> &builder,
	   	llvm::Module &M,
	   	std::string str)
{
	llvm::LLVMContext &Context = builder.getContext();
	llvm::Constant *StrConstant = llvm::ConstantDataArray::getString(Context, str);
	std::string name = std::string("__global_") + str;
	llvm::GlobalVariable *llvm_value = llvm_get_global(&M, name, StrConstant, true /*is_constant*/);
	return llvm_get_pointer_to_constant(builder, llvm_value);
}

llvm::Value *llvm_create_bool(llvm::IRBuilder<> &builder, bool value) {
	if (value) {
		return builder.getTrue();
	} else {
		return builder.getFalse();
	}
}

llvm::ConstantInt *llvm_create_int(llvm::IRBuilder<> &builder, int64_t value) {
	return builder.getZionInt(value);
}

llvm::ConstantInt *llvm_create_int16(llvm::IRBuilder<> &builder, int16_t value) {
	return builder.getInt16(value);
}

llvm::ConstantInt *llvm_create_int32(llvm::IRBuilder<> &builder, int32_t value) {
	return builder.getInt32(value);
}

llvm::Value *llvm_create_double(llvm::IRBuilder<> &builder, double value) {
	return llvm::ConstantFP::get(builder.getContext(), llvm::APFloat(value));
}

llvm::FunctionType *llvm_create_function_type(
		llvm::IRBuilder<> &builder,
		const bound_type_t::refs &args,
		bound_type_t::ref return_value)
{
	debug_above(4, log(log_info, "creating an LLVM function type from (%s %s)",
		::str(args).c_str(),
		return_value->str().c_str()));

	// assert(return_value->get_type()->ftv_count() == 0 && "return values should never be abstract");
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
		assert(llvm_value->getType()->isPointerTy());
		return alloca->getAllocatedType();
	} else {
		return llvm_value->getType();
	}
}

llvm::Value *_llvm_resolve_alloca(llvm::IRBuilder<> &builder, llvm::Value *llvm_value) {
	if (llvm::AllocaInst *alloca = llvm::dyn_cast<llvm::AllocaInst>(llvm_value)) {
		return builder.CreateLoad(alloca);
	} else {
		return llvm_value;
	}
}

bound_var_t::ref create_callsite(
		llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		life_t::ref life,
		const bound_var_t::ref function,
		std::string name,
		const location_t &location,
		bound_var_t::refs arguments)
{
	assert(function != nullptr);
	auto expanded_type = function->type->get_type()->eval(scope);
	auto closure = dyncast<const types::type_function_closure_t>(expanded_type);
	if (closure != nullptr) {
		debug_above(8, log("closure is %s", llvm_print(function->get_llvm_value()).c_str()));
		debug_above(8, log("closure type is %s", llvm_print(function->get_llvm_value()->getType()).c_str()));
		bound_type_t::ref var_ptr_type = scope->get_program_scope()->get_runtime_type(builder, STD_MANAGED_TYPE, true /*get_ptr*/);
		llvm::Type *llvm_var_ptr_type = var_ptr_type->get_llvm_type();

		/* we will be passing the "closure" to the function for its captured values */
		arguments.push_back(bound_var_t::create(
					INTERNAL_LOC(), "closure env",
					var_ptr_type,
					builder.CreateBitCast(function->get_llvm_value(), llvm_var_ptr_type),
					make_iid_impl("closure", function->get_location())));

		auto function_type = dyncast<const types::type_function_t>(closure->function);
		assert(function_type != nullptr);

		auto args = dyncast<const types::type_args_t>(function_type->args);
		assert(args != nullptr);

		auto augmented_args = args->args;
		augmented_args.push_back(var_ptr_type->get_type());

		auto augmented_names = args->names;
		if (augmented_names.size() != 0) {
			augmented_names.push_back(make_iid("__capture_env"));
		}

		types::type_function_t::ref inner_function_type = type_function(nullptr, type_args(augmented_args, augmented_names), function_type->return_type);

		auto bound_inner_function_type = upsert_bound_type(builder, scope, inner_function_type);

		std::vector<llvm::Value *> gep_path = std::vector<llvm::Value *>{builder.getInt32(0), builder.getInt32(1), builder.getInt32(0)};

		llvm::Value *llvm_inner_function = builder.CreateBitCast(
				builder.CreateLoad(builder.CreateInBoundsGEP(function->get_llvm_value(), gep_path)),
				bound_inner_function_type->get_llvm_specific_type());

		/* ok, we are actually calling a closure, so let's augment the existing arguments list with the closure object
		 * itself, then continue along, but do so by calling the inner function */
		bound_var_t::ref inner_function = bound_var_t::create(
				INTERNAL_LOC(), "inner function extraction",
				bound_inner_function_type, llvm_inner_function,
				make_iid_impl("inner function extraction", function->get_location()));

		return create_callsite(builder, scope, life, inner_function, name, location, arguments);
	} else {

#ifdef ZION_DEBUG
		llvm::Value *llvm_function = function->get_llvm_value();
		debug_above(5, log(log_info, "create_callsite is assuming %s is compatible with %s",
					function->get_type()->str().c_str(),
					str(arguments).c_str()));
		debug_above(5, log(log_info, "calling function " c_id("%s") " with type %s",
					function->name.c_str(),
					llvm_print(llvm_function->getType()).c_str()));
#endif

		/* downcast the arguments as necessary to var_t * */
		types::type_function_t::ref function_type = dyncast<const types::type_function_t>(
				function->get_type());

		if (function_type != nullptr) {
			auto return_type = upsert_bound_type(builder, scope, function_type->return_type);
			if (auto args = dyncast<const types::type_args_t>(function_type->args)) {
				auto coerced_parameter_values = get_llvm_values(builder,
						scope, life, location, args, arguments);
				llvm::CallInst *llvm_call_inst = llvm_create_call_inst(
						builder, location, function, coerced_parameter_values);

				bound_type_t::ref return_type = get_function_return_type(builder, scope, function->type);

				bound_var_t::ref ret = bound_var_t::create(INTERNAL_LOC(), name,
						return_type, llvm_call_inst,
						make_type_id_code_id(location, name));

				/* all return values must be tracked since the callee is
				 * expected to return a ref-counted value */
				life->track_var(builder, scope, ret, lf_statement);
				return ret;
			} else {
				panic("type args are not type_args_t");
				return nullptr;
			}
		} else {
			throw user_error(function->get_location(),
					"this expression is not callable (its type is %s)",
					function->type->str().c_str());
		}
	}
}

llvm::CallInst *llvm_create_call_inst(
		llvm::IRBuilder<> &builder,
		location_t location,
		ptr<const bound_var_t> callee,
		std::vector<llvm::Value *> llvm_values)
{
	assert(callee != nullptr);
	llvm::Value *llvm_callee_value = callee->get_llvm_value();
	debug_above(9, log("found llvm_callee_value %s of type %s",
				llvm_print(llvm_callee_value).c_str(),
				llvm_print(llvm_callee_value->getType()).c_str()));

	llvm::Value *llvm_function = nullptr;
	llvm::FunctionType *llvm_function_type = nullptr;
	llvm::Function *llvm_func_decl = nullptr;

	if (llvm::Function *llvm_callee_fn = llvm::dyn_cast<llvm::Function>(llvm_callee_value)) {
		/* see if we have an exact function we want to call */

		/* get the current module we're inserting code into */
		llvm::Module *llvm_module = llvm_get_module(builder);

		debug_above(3, log(log_info, "looking for function in LLVM " c_id("%s") " with type %s",
					llvm_callee_fn->getName().str().c_str(),
					llvm_print(llvm_callee_fn->getFunctionType()).c_str()));

		/* before we can call a function, we must make sure it either exists in
		 * this module, or a declaration exists */
		llvm_func_decl = llvm::cast<llvm::Function>(
				llvm_module->getOrInsertFunction(
					llvm_callee_fn->getName(),
					llvm_callee_fn->getFunctionType(),
					llvm_callee_fn->getAttributes()));

		llvm_function_type = llvm::dyn_cast<llvm::FunctionType>(llvm_func_decl->getType()->getElementType());
		llvm_function  = llvm_func_decl;
	} else {
		llvm_function = llvm_callee_value;

		llvm::PointerType *llvm_ptr_type = llvm::dyn_cast<llvm::PointerType>(llvm_callee_value->getType());
		assert(llvm_ptr_type != nullptr);

		debug_above(8, log("llvm_ptr_type is %s", llvm_print(llvm_ptr_type).c_str()));
		llvm_function_type = llvm::dyn_cast<llvm::FunctionType>(llvm_ptr_type->getElementType());
		assert(llvm_function_type != nullptr);
	}

	assert(llvm_function != nullptr);
	assert(llvm_function_type != nullptr);
	debug_above(3, log(log_info, "creating call to %s",
				llvm_print(llvm_function_type).c_str()));

#if 0
	auto param_iter = llvm_function_type->param_begin();
	std::vector<llvm::Value *> llvm_args;

	/* make one last pass over the parameters before we make this call */
	int index = 0;
	for (auto &llvm_value : llvm_values) {
		// assert(!llvm::dyn_cast<llvm::AllocaInst>(llvm_value));

		llvm::Value *llvm_arg = llvm_maybe_pointer_cast(
				builder,
				llvm_value,
				*param_iter);
		if (llvm_arg->getName().str().size() == 0) {
			llvm_arg->setName(string_format("arg.%d", index));
		}

		llvm_args.push_back(llvm_arg);

		++param_iter;
		++index;
	}
#endif
	llvm::ArrayRef<llvm::Value *> llvm_args_array(llvm_values);

	debug_above(3, log(log_info, "creating call to " c_id("%s") " %s with [%s]",
				llvm_func_decl ? llvm_func_decl->getName().str().c_str() : "a function",
				llvm_print(llvm_function_type).c_str(),
				join_with(llvm_values, ", ", llvm_print_value).c_str()));

	return builder.CreateCall(llvm_function, llvm_args_array);
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

std::string llvm_print_type(llvm::Type *llvm_type) {
	assert(llvm_type != nullptr);
	return llvm_print(llvm_type);
}

std::string llvm_print_value(llvm::Value *llvm_value) {
	assert(llvm_value != nullptr);
	return llvm_print(*llvm_value);
}

std::string llvm_print(llvm::Value *llvm_value) {
	assert(llvm_value != nullptr);
	return llvm_print(*llvm_value);
}

std::string llvm_print(llvm::Value &llvm_value) {
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

std::string llvm_print(llvm::Type *llvm_type) {
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
	   	std::string var_name)
{
	/* we'll need to place the alloca instance in the entry block, so let's
	 * make a builder that points there */
	llvm::IRBuilder<> builder(
			&llvm_function->getEntryBlock(),
		   	llvm_function->getEntryBlock().begin());

	/* create the local variable */
	return builder.CreateAlloca(type->get_llvm_specific_type(), nullptr, var_name.c_str());
}

llvm::AllocaInst *llvm_call_gcroot(
        llvm::Function *llvm_function,
        bound_type_t::ref type,
        std::string var_name)
{
    llvm::IRBuilder<> builder(
            &llvm_function->getEntryBlock(),
            llvm_function->getEntryBlock().begin());

    /* create the local variable */
    llvm::AllocaInst *llvm_alloca = builder.CreateAlloca(
            type->get_llvm_specific_type(), nullptr, var_name.c_str());
    auto module = llvm_get_module(builder);
    auto &context = builder.getContext();

    std::vector<llvm::Type *> arg_types;
    arg_types.push_back(llvm::PointerType::get(
                llvm::Type::getInt8PtrTy(context), 0));
    arg_types.push_back(llvm::Type::getInt8PtrTy(context));
    auto func_type = llvm::FunctionType::get(
            llvm::Type::getVoidTy(context), arg_types, false);

	module->getOrInsertFunction("llvm.gcroot", func_type);

    llvm::Value *v = builder.CreateBitCast(
            llvm_alloca, llvm::PointerType::get(
                llvm::PointerType::get(
                    llvm::Type::getInt8Ty(context), 0),
                0));

    std::vector<llvm::Value *> arg_vec;
    arg_vec.push_back(v);
    arg_vec.push_back(llvm::Constant::getNullValue(llvm::PointerType::get(
                    llvm::Type::getInt8Ty(context), 0)));
    builder.CreateCall(module->getFunction("llvm.gcroot"), arg_vec);

	/* set insertion point to the end of the entry block */
	auto llvm_terminator_inst = builder.GetInsertBlock()->getTerminator();
	assert(llvm_terminator_inst != nullptr);

	/* initialize this alloca as null, so that if we want to call runtime.gc somewhere in the middle
	 * of the function we don't read uninitialized data */

	builder.SetInsertPoint(llvm_terminator_inst);
	builder.CreateStore(llvm::Constant::getNullValue(type->get_llvm_specific_type()), llvm_alloca);

	// std::cerr << llvm_print(builder.GetInsertBlock()->getParent()) << std::endl;
    return llvm_alloca;
}

bound_var_t::ref llvm_stack_map_value(
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
        bound_var_t::ref value)
{
#ifdef ZION_DEBUG
	{
		bool is_managed;
		value->type->is_managed_ptr(builder, scope, is_managed);
		assert(is_managed);
	}
#endif

	if (value->type->is_ref(scope)) {
		return value;
	}

    llvm::Function *llvm_function = llvm_get_function(builder);
    auto name = string_format("stack_map.%s", value->name.c_str());
	/* put this stack variable into the shadow-stack */
	llvm::AllocaInst *llvm_alloca = llvm_call_gcroot(llvm_function, value->type, name);
	builder.CreateStore(
			value->resolve_bound_var_value(scope, builder),
			llvm_alloca);

	auto bound_type = upsert_bound_type(builder, scope, type_ref(value->type->get_type()));
	return bound_var_t::create(INTERNAL_LOC(),
			name, bound_type,
			llvm_alloca,
			make_iid(name));
}

bound_var_t::ref unmaybe_variable(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		std::string name,
		bound_var_t::ref var)
{
	bool was_ref = false;
	types::type_t::ref type = var->type->get_type();
	if (auto ref_type = dyncast<const types::type_ref_t>(type)) {
		was_ref = true;
		type = ref_type->element_type;
	}

	if (auto maybe_type = dyncast<const types::type_maybe_t>(type)) {
		auto bound_type = upsert_bound_type(builder, scope,
				was_ref ? type_ref(maybe_type->just) : maybe_type->just);
		return bound_var_t::create(INTERNAL_LOC(), name, bound_type,
				var->get_llvm_value(), make_iid_impl(name, location));
	} else {
		return var;
	}
}

void llvm_create_if_branch(
	   	llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		int iff,
		life_t::ref life,
		location_t location,
		bound_var_t::ref value,
		bool allow_maybe_check,
	   	llvm::BasicBlock *then_bb,
	   	llvm::BasicBlock *else_bb)
{
	/* the job of this function is to derive a value from the input value that is a valid input to a
	 * branch instruction */
	llvm::Function *llvm_function_current = llvm_get_function(builder);

	/* we don't care about references, load past them if need be */
	value = value->resolve_bound_value(builder, scope);

	llvm::Value *llvm_value = value->get_llvm_value();
	if (value->type->is_maybe(scope)) {
		if (allow_maybe_check) {
			llvm::Type *llvm_type = value->get_llvm_value()->getType();
			assert(llvm_type->isPointerTy());
			llvm::Constant *null = llvm::Constant::getNullValue(llvm_type);
			llvm_value = builder.CreateICmpNE(value->get_llvm_value(), null);
		} else {
			auto error = user_error(location, "implicit maybe checks are not allowed here");
			error.add_info(location, "the condition of this branch instruction is of type %s",
					value->type->str().c_str());
			throw error;
		}
	}

	if (llvm_value->getType()->isIntegerTy(1)) {
		/* pass */
	} else {
		types::type_t::ref type = value->type->get_type();

		if (types::is_type_id(type, TRUE_TYPE, nullptr)) {
			llvm_value = llvm::ConstantInt::get(builder.getIntNTy(1), 1);
		} else if (types::is_type_id(type, FALSE_TYPE, nullptr)) {
			llvm_value = llvm::ConstantInt::get(builder.getIntNTy(1), 0);
		} else if (types::is_type_id(type, BOOL_TYPE, nullptr)) {
			llvm::Type *llvm_type = llvm_value->getType();
			assert(llvm_type->isIntegerTy());
			if (!llvm_type->isIntegerTy(1)) {
				llvm::Constant *zero = llvm::ConstantInt::get(llvm_type, 0);
				llvm_value = builder.CreateICmpNE(llvm_value, zero);
			}
			assert(llvm_value->getType()->isIntegerTy(1));
		} else {
			user_error error = user_error(
					location,
				   	allow_maybe_check ? "condition is not a boolean value or a nullable pointer type (*?)" : "condition is not a boolean value");
			error.add_info(location, "the condition of this branch instruction is of type %s", value->type->str().c_str());
			error.add_info(value->get_location(), "the value was defined here");
			throw error;
		}
	}

	assert(llvm_value->getType()->isIntegerTy(1));

	// REVIEW: do we need these extra blocks now?
	if (iff & IFF_ELSE) {
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);

		llvm::BasicBlock *release_block_bb = llvm::BasicBlock::Create(
				builder.getContext(), "else.release", llvm_function_current);
		builder.SetInsertPoint(release_block_bb);
		life->release_vars(builder, scope, lf_statement);

		assert(!builder.GetInsertBlock()->getTerminator());
		builder.CreateBr(else_bb);

		/* trick the code below to jumping to this release guard block */
		else_bb = release_block_bb;
	}

	if (iff & IFF_THEN) {
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);

		llvm::BasicBlock *release_block_bb = llvm::BasicBlock::Create(
				builder.getContext(), "then.release", llvm_function_current);
		builder.SetInsertPoint(release_block_bb);
		life->release_vars(builder, scope, lf_statement);

		assert(!builder.GetInsertBlock()->getTerminator());
		builder.CreateBr(then_bb);

		/* trick the code below to jumping to this release guard block */
		then_bb = release_block_bb;
	}

	builder.CreateCondBr(llvm_value, then_bb, else_bb);
}

bound_var_t::ref create_global_str(
		llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
	   	location_t location,
	   	std::string value)
{
	auto program_scope = scope->get_program_scope();

	bound_type_t::ref str_type = upsert_bound_type(builder, scope, type_id(make_iid_impl(MANAGED_STR, location)));
	bound_type_t::ref str_literal_type = program_scope->get_runtime_type(builder, "str_literal_t", true /*get_ptr*/);
	bound_type_t::ref owning_buffer_literal_type = program_scope->get_runtime_type(builder, "owning_buffer_literal_t", true /*get_ptr*/);
	bound_type_t::ref type_info_type = program_scope->get_runtime_type(builder, "type_info_t", true /*get_ptr*/);

	llvm::Module *llvm_module = scope->get_llvm_module();

	std::string owning_buffer_type_info_name = "__internal.owning_buffer_literal_type_info";
	bound_var_t::ref owning_buffer_literal_type_info = program_scope->get_bound_variable(builder, location, owning_buffer_type_info_name);
	llvm::Constant *llvm_owning_buffer_type_info;

	if (owning_buffer_literal_type_info == nullptr) {
		debug_above(8, log("creating owning buffer type info"));
		llvm_owning_buffer_type_info = llvm_get_global(
				llvm_module,
				"owning_buffer_literal_type_info",
				llvm_create_constant_struct_instance(
					llvm::dyn_cast<llvm::StructType>(
						type_info_type->get_llvm_type()->getPointerElementType()),
						{
							llvm_create_int32(builder, type_kind_no_gc),
							llvm_create_int(builder, 0/*size*/),
							(llvm::Constant *)builder.CreateGlobalStringPtr("owning-buffer-literal"/*name*/),
						}),
				true /*isConstant*/);
		program_scope->put_bound_variable(owning_buffer_type_info_name, bound_var_t::create(
					INTERNAL_LOC(),
					owning_buffer_type_info_name,
					type_info_type,
					llvm_owning_buffer_type_info,
					make_iid_impl(owning_buffer_type_info_name, location)));
	} else {
		llvm_owning_buffer_type_info = (llvm::Constant *)owning_buffer_literal_type_info->get_llvm_value();
	}

	debug_above(8, log("creating owning buffer for string literal \"%s\"", value.c_str()));

	std::string owning_buffer_literal_name = string_format("__internal.owning_buffer_literal_%d", atomize(value));
	bound_var_t::ref owning_buffer_literal = program_scope->get_bound_variable(builder, location, owning_buffer_literal_name);
	llvm::Constant *llvm_owning_buffer_literal;

	if (owning_buffer_literal == nullptr) {
		llvm_owning_buffer_literal = llvm_get_global(
				llvm_module,
				owning_buffer_literal_name,
				llvm_create_constant_struct_instance(
					llvm::dyn_cast<llvm::StructType>(owning_buffer_literal_type->get_llvm_type()->getPointerElementType()),
					{
					llvm_owning_buffer_type_info,
					llvm_create_int(builder, 0),
					llvm::Constant::getNullValue(builder.getInt8Ty()->getPointerTo()),
					llvm::Constant::getNullValue(builder.getInt8Ty()->getPointerTo()),
					llvm_create_int(builder, 0),
					builder.getInt32(atomize("OwningBuffer")),
					(llvm::Constant *)builder.CreateGlobalStringPtr(value),
					llvm_create_int(builder, value.size()),
					}),
				true /*isConstant*/);
		program_scope->put_bound_variable(owning_buffer_literal_name, bound_var_t::create(
					INTERNAL_LOC(),
					owning_buffer_literal_name,
					owning_buffer_literal_type,
					llvm_owning_buffer_literal,
					make_iid_impl(owning_buffer_literal_name, location)));
	} else {
		llvm_owning_buffer_literal = (llvm::Constant *)owning_buffer_literal->get_llvm_value();
	}

	debug_above(8, log("creating str type info for string literal \"%s\"", value.c_str()));

	std::string str_literal_type_info_name = "__internal.str_literal_type_info";
	bound_var_t::ref str_literal_type_info = program_scope->get_bound_variable(builder, location, str_literal_type_info_name);
	llvm::Constant *llvm_str_type_info;
	if (str_literal_type_info == nullptr) {
		llvm_str_type_info = llvm_get_global(
				llvm_module,
				str_literal_type_info_name,
				llvm_create_constant_struct_instance(
					llvm::dyn_cast<llvm::StructType>(type_info_type->get_llvm_type()->getPointerElementType()),
					{
					llvm_create_int32(builder, type_kind_no_gc),
					llvm_create_int(builder, 0/*size*/),
					(llvm::Constant *)builder.CreateGlobalStringPtr("string-literal"/*name*/),
					}),
				true /*isConstant*/);
		program_scope->put_bound_variable(str_literal_type_info_name, bound_var_t::create(
					INTERNAL_LOC(),
					str_literal_type_info_name,
					type_info_type,
					llvm_str_type_info,
					make_iid_impl(str_literal_type_info_name, location)));
	} else {
		llvm_str_type_info = (llvm::Constant *)str_literal_type_info->get_llvm_value();
	}

	debug_above(8, log("creating str literal \"%s\"", value.c_str()));
	std::string str_literal_name = string_format("__internal.str_literal_%d", atomize(value));
	bound_var_t::ref str_literal = program_scope->get_bound_variable(builder, location, str_literal_name);
	llvm::Constant *llvm_str_literal;

	if (str_literal == nullptr) {
		llvm_str_literal = llvm_get_global(
				llvm_module,
				"str_literal",
				llvm_create_constant_struct_instance(
					llvm::dyn_cast<llvm::StructType>(str_literal_type->get_llvm_type()->getPointerElementType()),
					{
					llvm_str_type_info,
					llvm_create_int(builder, 0),
					llvm::Constant::getNullValue(builder.getInt8Ty()->getPointerTo()),
					llvm::Constant::getNullValue(builder.getInt8Ty()->getPointerTo()),
					llvm_create_int(builder, 0),
					builder.getInt32(atomize(MANAGED_STR)),
					llvm_owning_buffer_literal,
					llvm_create_int(builder, 0),
					llvm_create_int(builder, value.size()),
					}),
				true /*isConstant*/);
		str_literal = bound_var_t::create(
				INTERNAL_LOC(),
				str_literal_name,
				str_type,
				builder.CreateBitCast(llvm_str_literal, str_type->get_llvm_type()),
				make_iid_impl(str_literal_name, location));
		program_scope->put_bound_variable(str_literal_name, str_literal);
	}

	return str_literal;
}

llvm::Constant *llvm_create_struct_instance(
		std::string var_name,
		llvm::Module *llvm_module,
		llvm::StructType *llvm_struct_type, 
		std::vector<llvm::Constant *> llvm_struct_data)
{
	debug_above(5, log("creating struct %s with %s",
			llvm_print(llvm_struct_type).c_str(),
			join_with(llvm_struct_data, ", ",
			   	[] (llvm::Constant *c) -> std::string {
					return llvm_print(c);
					}).c_str()));

	return llvm_get_global(
			llvm_module, var_name,
			llvm_create_constant_struct_instance(llvm_struct_type, llvm_struct_data),
			true /*is_constant*/);
}

llvm::Constant *llvm_create_constant_struct_instance(
		llvm::StructType *llvm_struct_type, 
		std::vector<llvm::Constant *> llvm_struct_data)
{
	assert(llvm_struct_type != nullptr);
	llvm::ArrayRef<llvm::Constant*> llvm_struct_initializer{llvm_struct_data};
	check_struct_initialization(llvm_struct_initializer, llvm_struct_type);

	return llvm::ConstantStruct::get(llvm_struct_type, llvm_struct_data);
}

llvm::StructType *llvm_create_struct_type(
		llvm::IRBuilder<> &builder,
		std::string name,
		const std::vector<llvm::Type*> &llvm_types)
{
	llvm::ArrayRef<llvm::Type*> llvm_dims{llvm_types};

	auto llvm_struct_type = llvm::StructType::create(builder.getContext(), llvm_dims);

	/* give the struct a helpful name internally */
	llvm_struct_type->setName(name);

	debug_above(3, log(log_info, "created struct type " c_id("%s") " %s",
				name.c_str(),
				llvm_print(llvm_struct_type).c_str()));

	return llvm_struct_type;
}

llvm::StructType *llvm_create_struct_type(
		llvm::IRBuilder<> &builder,
		std::string name,
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

void llvm_verify_function(location_t location, llvm::Function *llvm_function) {
	std::stringstream ss;
	llvm::raw_os_ostream os(ss);
	if (llvm::verifyFunction(*llvm_function, &os)) {
		os.flush();
		ss << llvm_print_function(llvm_function);
		debug_above(5, log("writing to function-verification-failure.llir..."));
		std::string llir_filename = "function-verification-failure.llir";
		FILE *fp = fopen(llir_filename.c_str(), "wt");
		fprintf(fp, "%s\n", llvm_print_module(*llvm_function->getParent()).c_str());
		fclose(fp);
		auto error = user_error(location, "LLVM function verification failed: %s", ss.str().c_str());
		error.add_info(location_t{llir_filename, 1, 1}, "consult LLVM module dump");
		throw error;
	}
}

void llvm_verify_module(llvm::Module &llvm_module) {
	std::stringstream ss;
	llvm::raw_os_ostream os(ss);
	if (llvm::verifyModule(llvm_module, &os)) {
		os.flush();
		throw user_error(location_t{}, "module %s: failed verification. %s\nModule listing:\n%s",
				llvm_module.getName().str().c_str(),
				ss.str().c_str(),
				llvm_print_module(llvm_module).c_str());

	}
}

llvm::Constant *llvm_sizeof_type(llvm::IRBuilder<> &builder, llvm::Type *llvm_type) {
	llvm::StructType *llvm_struct_type = llvm::dyn_cast<llvm::StructType>(llvm_type);
	if (llvm_struct_type != nullptr) {
		if (llvm_struct_type->isOpaque()) {
			debug_above(1, log("llvm_struct_type is opaque when we're trying to get its size: %s",
						llvm_print(llvm_struct_type).c_str()));
			assert(false);
		}
		assert(llvm_struct_type->elements().size() != 0);
	}

	llvm::Constant *alloc_size_const = llvm::ConstantExpr::getSizeOf(llvm_type);
	llvm::Constant *size_value = llvm::ConstantExpr::getTruncOrBitCast(alloc_size_const, builder.getInt64Ty());
	debug_above(3, log(log_info, "size of %s is: %s", llvm_print(llvm_type).c_str(),
				llvm_print(*size_value).c_str()));
	return size_value;

}

llvm::Type *llvm_deref_type(llvm::Type *llvm_type) {
	if (llvm_type->isPointerTy()) {
		return llvm::cast<llvm::PointerType>(llvm_type)->getElementType();
	} else {
		return llvm_type;
	}
}

bound_var_t::ref llvm_start_function(
		llvm::IRBuilder<> &builder, 
		scope_t::ref scope,
		location_t location,
		const types::type_function_t::ref &function_type,
		std::string name)
{
	types::type_args_t::ref type_args = dyncast<const types::type_args_t>(function_type->args);
	assert(type_args != nullptr);

	bound_type_t::refs args = upsert_bound_types(builder, scope, type_args->args);
	bound_type_t::ref data_type = upsert_bound_type(builder, scope, function_type->return_type);
	/* get the llvm function type for the data ctor */
	llvm::FunctionType *llvm_fn_type = llvm_create_function_type(
			builder, args, data_type);

	/* create the bound type for the ctor function */
	auto bound_function_type = bound_type_t::create(function_type, location, llvm_fn_type);

	/* now let's generate our actual data ctor fn */
	auto llvm_function = llvm::Function::Create(
			(llvm::FunctionType *)llvm_fn_type,
			llvm::Function::ExternalLinkage, name,
			scope->get_llvm_module());

	llvm_function->setGC(GC_STRATEGY);
	llvm_function->setDoesNotThrow();

	/* create the actual bound variable for the fn */
	bound_var_t::ref function = bound_var_t::create(
			INTERNAL_LOC(), name,
			bound_function_type, llvm_function, make_iid_impl(name, location));

	/* start emitting code into the new function. caller should have an
	 * insert point guard */
	llvm::BasicBlock *llvm_entry_block = llvm::BasicBlock::Create(builder.getContext(),
			"entry", llvm_function);
	llvm::BasicBlock *llvm_body_block = llvm::BasicBlock::Create(builder.getContext(),
			"body", llvm_function);

	builder.SetInsertPoint(llvm_entry_block);
	/* leave an empty entry block so that we can insert GC stuff in there, but be able to
	 * seek to the end of it and not get into business logic */
	builder.CreateBr(llvm_body_block);

	builder.SetInsertPoint(llvm_body_block);

	return function;
}

void check_struct_initialization(
		llvm::ArrayRef<llvm::Constant*> llvm_struct_initialization,
		llvm::StructType *llvm_struct_type)
{
	if (llvm_struct_type->elements().size() != llvm_struct_initialization.size()) {
		debug_above(7, log(log_error, "mismatch in number of elements for %s (%d != %d)",
					llvm_print(llvm_struct_type).c_str(),
					(int)llvm_struct_type->elements().size(),
					(int)llvm_struct_initialization.size()));
		assert(false);
	}

	for (unsigned i = 0, e = llvm_struct_initialization.size(); i != e; ++i) {
		if (llvm_struct_initialization[i]->getType() == llvm_struct_type->getElementType(i)) {
			continue;
		} else {
			debug_above(7, log(log_error, "llvm_struct_initialization[%d] mismatch is %s should be %s",
						i,
						llvm_print(*llvm_struct_initialization[i]).c_str(),
						llvm_print(llvm_struct_type->getElementType(i)).c_str()));
			assert(false);
		}
	}
}

llvm::GlobalVariable *llvm_get_global(
		llvm::Module *llvm_module,
		std::string name,
		llvm::Constant *llvm_constant,
		bool is_constant)
{
	auto llvm_global_variable = new llvm::GlobalVariable(*llvm_module,
			llvm_constant->getType(),
			is_constant, llvm::GlobalValue::PrivateLinkage,
			llvm_constant, name, nullptr,
			llvm::GlobalVariable::NotThreadLocal);

	// llvm_global_variable->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
	return llvm_global_variable;
}

bound_var_t::ref llvm_create_global_tag(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_type_t::ref tag_type,
		std::string tag,
		identifier::ref id)
{
	auto program_scope = scope->get_program_scope();

	/* For a tag called "Example" with a type_id of 42, the LLIR should look
	 * like this:
	 *
	 * @__tag_type_info_Example = global %struct.type_info_t { i32 42, i16 -1, i16* null, i8* getelementptr inbounds ([5 x i8], [5 x i8]* @.str, i32 0, i32 0), i16 0 }, align 8
	 * @__tag_Example = global %struct.tag_t { %struct.type_info_t* @__tag_type_info_Example }, align 8
	 * @Example = global %struct.var_t* bitcast (%struct.tag_t* @__tag_Example to %struct.var_t*), align 8 */

	bound_type_t::ref var_ptr_type = program_scope->get_runtime_type(builder, STD_MANAGED_TYPE, true /*get_ptr*/);
	llvm::Type *llvm_var_ptr_type = var_ptr_type->get_llvm_type();

	llvm::StructType *llvm_tag_struct_type = llvm::dyn_cast<llvm::StructType>(llvm_var_ptr_type->getPointerElementType());
	debug_above(10, log(log_info, "var_ptr_type is %s", llvm_print(var_ptr_type->get_llvm_type()).c_str()));
	debug_above(10, log(log_info, "tag_struct_type is %s", llvm_print(llvm_tag_struct_type).c_str()));
	assert(llvm_tag_struct_type != nullptr);

	llvm::Module *llvm_module = scope->get_llvm_module();
	assert(llvm_module != nullptr);

	llvm::Constant *llvm_name = llvm_create_global_string_constant(builder, *llvm_module, tag);
	debug_above(10, log(log_info, "llvm_name is %s", llvm_print(*llvm_name).c_str()));

	bound_type_t::ref type_info_type = program_scope->get_runtime_type(builder, "type_info_t");
	llvm::StructType *llvm_type_info_type = llvm::dyn_cast<llvm::StructType>(
			type_info_type->get_llvm_type());
	assert(llvm_type_info_type != nullptr);

	/* create the type information inside the tag singleton */
	llvm::Constant *llvm_type_info = llvm_create_struct_instance(
			std::string("__tag_type_info_") + tag,
			llvm_module,
			llvm_type_info_type,
			{
			/* the type kind */
			builder.getInt32(type_kind_no_gc),

			/* size - should always be zero since the type_id is part of this var_t
			 * as builtin type info. */
			builder.getInt64(0),

			/* name - for debugging */
			llvm_name,
			});

	std::vector<llvm::Constant *> llvm_struct_data_tag = {
		llvm_type_info,
		llvm_create_int(builder, 0),
		llvm::Constant::getNullValue(llvm_tag_struct_type->getPointerTo()),
		llvm::Constant::getNullValue(llvm_tag_struct_type->getPointerTo()),
		llvm_create_int(builder, 0),
		builder.getInt32(atomize(tag)),
	};

	/* create the actual tag singleton */
	llvm::Constant *llvm_tag_constant = llvm_create_struct_instance(
			std::string("__tag_") + tag,
			llvm_module,
			llvm_tag_struct_type,
			llvm_struct_data_tag);

	return bound_var_t::create(INTERNAL_LOC(), tag, tag_type, llvm_tag_constant, id);
}

llvm::Value *llvm_maybe_pointer_cast(
		llvm::IRBuilder<> &builder,
	   	llvm::Value *llvm_value,
	   	llvm::Type *llvm_type)
{
	if (llvm_value->getType() == llvm_type) {
		return llvm_value;
	}

	if (llvm_type->isPointerTy()) {
		debug_above(6, log("attempting to cast %s to a %s",
					llvm_print(llvm_value).c_str(),
					llvm_print(llvm_type).c_str()));
		assert(llvm_value->getType()->isPointerTy() || llvm_value->getType()->isIntegerTy());
		assert(llvm_value->getType() != llvm_type->getPointerTo());

		if (llvm_type != llvm_value->getType()) {
			return builder.CreateBitCast(llvm_value, llvm_type);
		}
	}

	return llvm_value;
}

llvm::Value *llvm_int_cast(
		llvm::IRBuilder<> &builder,
		llvm::Value *llvm_value,
		llvm::Type *llvm_type)
{
	return builder.CreateIntCast(llvm_value, llvm_type, false /*isSigned*/);
}

llvm::Value *llvm_maybe_pointer_cast(
		llvm::IRBuilder<> &builder,
		llvm::Value *llvm_value,
		const bound_type_t::ref &bound_type)
{
	return llvm_maybe_pointer_cast(builder, llvm_value, bound_type->get_llvm_specific_type());
}

void explain(llvm::Type *llvm_type) {
	INDENT(6,
			string_format("explain %s",
				llvm_print(llvm_type).c_str()));

	if (auto llvm_struct_type = llvm::dyn_cast<llvm::StructType>(llvm_type)) {
		for (auto element: llvm_struct_type->elements()) {
			explain(element);
		}
	} else if (auto lp = llvm::dyn_cast<llvm::PointerType>(llvm_type)) {
		explain(lp->getElementType());
	}
}

bool llvm_value_is_handle(llvm::Value *llvm_value) {
    llvm::Type *llvm_type = llvm_value->getType();
    return llvm_type->isPointerTy() && llvm::cast<llvm::PointerType>(llvm_type)->getElementType()->isPointerTy();
}

bool llvm_value_is_pointer(llvm::Value *llvm_value) {
    llvm::Type *llvm_type = llvm_value->getType();
    return llvm_type->isPointerTy();
}

llvm::StructType *llvm_find_struct(llvm::Type *llvm_type) {
	if (auto llvm_struct_type = llvm::dyn_cast<llvm::StructType>(llvm_type)) {
		return llvm_struct_type;
	} else if (auto llvm_ptr_type = llvm::dyn_cast<llvm::PointerType>(llvm_type)) {
		return llvm_find_struct(llvm_ptr_type->getElementType());
	} else {
		return nullptr;
	}
}

void llvm_generate_dead_return(llvm::IRBuilder<> &builder, scope_t::ref scope) {
	llvm::Function *llvm_function_current = llvm_get_function(builder);
	llvm::Type *llvm_return_type = llvm_function_current->getReturnType();
	if (llvm_return_type->isPointerTy()) {
		builder.CreateRet(llvm::Constant::getNullValue(llvm_return_type));
	} else if (llvm_return_type->isIntegerTy()) {
		builder.CreateRet(llvm::ConstantInt::get(llvm_return_type, 0));
	} else if (llvm_return_type->isVoidTy()) {
		builder.CreateRetVoid();
	} else if (llvm_return_type->isFloatTy()) {
		builder.CreateRet(llvm::ConstantFP::get(llvm_return_type, 0.0));
	} else if (llvm_return_type->isDoubleTy()) {
		builder.CreateRet(llvm::ConstantFP::get(llvm_return_type, 0.0));
	} else {
		log(log_error, "unhandled return type for dead return %s", llvm_print(llvm_return_type).c_str());
		assert(false && "Unhandled return type.");
	}
}
