#include "llvm_utils.h"

#include <iostream>

#include "builtins.h"
#include "compiler.h"
#include "location.h"
#include "logger.h"
#include "utils.h"

namespace zion {

llvm::Value *llvm_create_global_string(llvm::IRBuilder<> &builder,
                                       std::string value) {
  return builder.CreateGlobalStringPtr(value);
}

llvm::Constant *llvm_get_pointer_to_constant(llvm::IRBuilder<> &builder,
                                             llvm::Constant *llvm_constant) {
  assert(llvm::dyn_cast<llvm::PointerType>(llvm_constant->getType()));

  debug_above(9, log(log_info, "getting pointer to constant %s",
                     llvm_print(llvm_constant).c_str()));

  std::vector<llvm::Constant *> gep_indices = {builder.getInt32(0),
                                               builder.getInt32(0)};

  return llvm::ConstantExpr::getInBoundsGetElementPtr(nullptr, llvm_constant,
                                                      gep_indices);
}

llvm::Constant *llvm_create_global_string_constant(llvm::IRBuilder<> &builder,
                                                   llvm::Module &M,
                                                   std::string str) {
  llvm::LLVMContext &Context = builder.getContext();
  llvm::Constant *StrConstant = llvm::ConstantDataArray::getString(Context,
                                                                   str);
  std::string name = std::string("__global_string_") + str;
  llvm::GlobalVariable *llvm_value = llvm_get_global(&M, name, StrConstant,
                                                     true /*is_constant*/);
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

llvm::ConstantInt *llvm_create_int16(llvm::IRBuilder<> &builder,
                                     int16_t value) {
  return builder.getInt16(value);
}

llvm::ConstantInt *llvm_create_int32(llvm::IRBuilder<> &builder,
                                     int32_t value) {
  return builder.getInt32(value);
}

llvm::Value *llvm_create_double(llvm::IRBuilder<> &builder, double value) {
  return llvm::ConstantFP::get(builder.getContext(), llvm::APFloat(value));
}

llvm::Type *llvm_resolve_type(llvm::Value *llvm_value) {
  if (llvm::AllocaInst *alloca = llvm::dyn_cast<llvm::AllocaInst>(llvm_value)) {
    assert(llvm_value->getType()->isPointerTy());
    return alloca->getAllocatedType();
  } else {
    return llvm_value->getType();
  }
}

llvm::Value *_llvm_resolve_alloca(llvm::IRBuilder<> &builder,
                                  llvm::Value *llvm_value) {
  if (llvm::AllocaInst *alloca = llvm::dyn_cast<llvm::AllocaInst>(llvm_value)) {
    return builder.CreateLoad(alloca);
  } else {
    return llvm_value;
  }
}

llvm::CallInst *llvm_create_call_inst(llvm::IRBuilder<> &builder,
                                      llvm::Value *llvm_callee_value,
                                      std::vector<llvm::Value *> llvm_values) {
  debug_above(9, log("found llvm_callee_value %s of type %s",
                     llvm_print(llvm_callee_value).c_str(),
                     llvm_print(llvm_callee_value->getType()).c_str()));

  llvm::Value *llvm_function = nullptr;
  llvm::FunctionType *llvm_function_type = nullptr;
  llvm::Function *llvm_func_decl = nullptr;

  if (llvm::Function *llvm_callee_fn = llvm::dyn_cast<llvm::Function>(
          llvm_callee_value)) {
    /* see if we have an exact function we want to call */

    /* get the current module we're inserting code into */
    llvm::Module *llvm_module = llvm_get_module(builder);

    debug_above(3,
                log(log_info,
                    "looking for function in LLVM " c_id("%s") " with type %s",
                    llvm_callee_fn->getName().str().c_str(),
                    llvm_print(llvm_callee_fn->getFunctionType()).c_str()));

    /* before we can call a function, we must make sure it either exists in
     * this module, or a declaration exists */
    llvm_func_decl = llvm::cast<llvm::Function>(
        llvm_module->getOrInsertFunction(llvm_callee_fn->getName(),
                                         llvm_callee_fn->getFunctionType(),
                                         llvm_callee_fn->getAttributes()));

    llvm_function_type = llvm::dyn_cast<llvm::FunctionType>(
        llvm_func_decl->getType()->getElementType());
    llvm_function = llvm_func_decl;
  } else {
    llvm_function = llvm_callee_value;

    llvm::PointerType *llvm_ptr_type = llvm::dyn_cast<llvm::PointerType>(
        llvm_callee_value->getType());
    assert(llvm_ptr_type != nullptr);

    debug_above(8,
                log("llvm_ptr_type is %s", llvm_print(llvm_ptr_type).c_str()));
    llvm_function_type = llvm::dyn_cast<llvm::FunctionType>(
        llvm_ptr_type->getElementType());
    assert(llvm_function_type != nullptr);
  }

  assert(llvm_function != nullptr);
  assert(llvm_function_type != nullptr);
  debug_above(3, log(log_info, "creating call to %s",
                     llvm_print(llvm_function_type).c_str()));

  if (llvm_function_type->getNumParams() - 1 == llvm_values.size()) {
    /* no closure, but we need to pad the inputs in this case. */
    llvm_values.push_back(
        llvm::Constant::getNullValue(builder.getInt8Ty()->getPointerTo()));
  }

  llvm::ArrayRef<llvm::Value *> llvm_args_array(llvm_values);

  debug_above(3, log(log_info, "creating call to " c_id("%s") " %s with [%s]",
                     llvm_func_decl ? llvm_func_decl->getName().str().c_str()
                                    : "a function",
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
  assert(ss.str().find("<badref>") == std::string::npos);
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

llvm::AllocaInst *llvm_create_entry_block_alloca(llvm::Function *llvm_function,
                                                 types::TypeEnv &type_env,
                                                 types::Ref type,
                                                 std::string var_name) {
  /* we'll need to place the alloca instance in the entry block, so let's
   * make a builder that points there */
  llvm::IRBuilder<> builder(&llvm_function->getEntryBlock(),
                            llvm_function->getEntryBlock().begin());

  /* create the local variable */
  return builder.CreateAlloca(get_llvm_type(builder, type_env, type), nullptr,
                              var_name.c_str());
}

llvm::Value *llvm_zion_bool_to_i1(llvm::IRBuilder<> &builder,
                                  llvm::Value *llvm_value) {
  if (llvm_value->getType()->isIntegerTy(1)) {
    return llvm_value;
  }

  llvm::Type *llvm_type = llvm_value->getType();
  assert(llvm_type->isIntegerTy());
  if (!llvm_type->isIntegerTy(1)) {
    llvm::Constant *zero = llvm::ConstantInt::get(llvm_type, 0);
    llvm_value = builder.CreateICmpNE(llvm_value, zero);
  }
  assert(llvm_value->getType()->isIntegerTy(1));
  return llvm_value;
}

void llvm_create_if_branch(llvm::IRBuilder<> &builder,
                           llvm::Value *llvm_value,
                           llvm::BasicBlock *then_bb,
                           llvm::BasicBlock *else_bb) {
  /* the job of this function is to derive a value from the input value that is
   * a valid input to a branch instruction */
  builder.CreateCondBr(llvm_zion_bool_to_i1(builder, llvm_value), then_bb,
                       else_bb);
}

llvm::Constant *llvm_create_struct_instance(
    std::string var_name,
    llvm::Module *llvm_module,
    llvm::StructType *llvm_struct_type,
    std::vector<llvm::Constant *> llvm_struct_data) {
  debug_above(5, log("creating struct %s with %s",
                     llvm_print(llvm_struct_type).c_str(),
                     join_with(llvm_struct_data, ", ",
                               [](llvm::Constant *c) -> std::string {
                                 return llvm_print(c);
                               })
                         .c_str()));

  return llvm_get_global(
      llvm_module, var_name,
      llvm_create_constant_struct_instance(llvm_struct_type, llvm_struct_data),
      true /*is_constant*/);
}

llvm::Constant *llvm_create_constant_struct_instance(
    llvm::StructType *llvm_struct_type,
    std::vector<llvm::Constant *> llvm_struct_data) {
  assert(llvm_struct_type != nullptr);
  llvm::ArrayRef<llvm::Constant *> llvm_struct_initializer{llvm_struct_data};
  check_struct_initialization(llvm_struct_initializer, llvm_struct_type);

  return llvm::ConstantStruct::get(llvm_struct_type, llvm_struct_data);
}

llvm::StructType *llvm_create_struct_type(
    llvm::IRBuilder<> &builder,
    const std::vector<llvm::Type *> &llvm_types_) {
  llvm::ArrayRef<llvm::Type *> llvm_types{llvm_types_};
  auto llvm_struct_type = llvm::StructType::get(builder.getContext(),
                                                llvm_types);

  debug_above(3, log(log_info, "created struct type %s",
                     llvm_print(llvm_struct_type).c_str()));

  return llvm_struct_type;
}

llvm::StructType *llvm_create_struct_type(llvm::IRBuilder<> &builder,
                                          types::TypeEnv &type_env,
                                          const types::Refs &dimensions) {
  return llvm_create_struct_type(builder,
                                 get_llvm_types(builder, type_env, dimensions));
}

llvm::StructType *llvm_create_struct_type(
    llvm::IRBuilder<> &builder,
    const std::vector<llvm::Value *> &llvm_dims) {
  std::vector<llvm::Type *> llvm_types;
  for (auto llvm_dim : llvm_dims) {
    llvm_types.push_back(llvm_dim->getType());
  }
  return llvm_create_struct_type(builder, llvm_types);
}

void llvm_verify_function(Location location, llvm::Function *llvm_function) {
  debug_above(5, log("writing to function-verification-failure.ll..."));
  std::string llir_filename = "function-verification-failure.ll";
#if 1
  FILE *fp = fopen(llir_filename.c_str(), "wt");
  fprintf(fp, "%s\n", llvm_print_module(*llvm_function->getParent()).c_str());
  fclose(fp);
#endif

  std::stringstream ss;
  llvm::raw_os_ostream os(ss);
  if (llvm::verifyFunction(*llvm_function, &os)) {
    os.flush();
    ss << llvm_print_function(llvm_function);
    auto error = user_error(location, "LLVM function verification failed: %s",
                            ss.str().c_str());
    error.add_info(Location{llir_filename, 1, 1}, "consult LLVM module dump");
    throw error;
  }
}

void llvm_verify_module(llvm::Module &llvm_module) {
  std::stringstream ss;
  llvm::raw_os_ostream os(ss);
  if (llvm::verifyModule(llvm_module, &os)) {
    os.flush();
    throw user_error(Location{},
                     "module %s: failed verification. %s\nModule listing:\n%s",
                     llvm_module.getName().str().c_str(), ss.str().c_str(),
                     llvm_print_module(llvm_module).c_str());
  }
  // std::cerr << ss.str() << std::endl;
}

llvm::Constant *llvm_sizeof_type(llvm::IRBuilder<> &builder,
                                 llvm::Type *llvm_type) {
  llvm::StructType *llvm_struct_type = llvm::dyn_cast<llvm::StructType>(
      llvm_type);
  if (llvm_struct_type != nullptr) {
    if (llvm_struct_type->isOpaque()) {
      debug_above(1, log("llvm_struct_type is opaque when we're trying to get "
                         "its size: %s",
                         llvm_print(llvm_struct_type).c_str()));
      assert(false);
    }
    assert(llvm_struct_type->elements().size() != 0);
  }

  llvm::Constant *alloc_size_const = llvm::ConstantExpr::getSizeOf(llvm_type);
  llvm::Constant *size_value = llvm::ConstantExpr::getTruncOrBitCast(
      alloc_size_const, builder.getInt64Ty());
  size_value->setName("size_value");
  debug_above(3,
              log(log_info, "size of %s is: %s", llvm_print(llvm_type).c_str(),
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

void check_struct_initialization(
    llvm::ArrayRef<llvm::Constant *> llvm_struct_initialization,
    llvm::StructType *llvm_struct_type) {
  if (llvm_struct_type->elements().size() !=
      llvm_struct_initialization.size()) {
    debug_above(7, log(log_error,
                       "mismatch in number of elements for %s (%d != %d)",
                       llvm_print(llvm_struct_type).c_str(),
                       (int)llvm_struct_type->elements().size(),
                       (int)llvm_struct_initialization.size()));
    assert(false);
  }

  for (unsigned i = 0, e = llvm_struct_initialization.size(); i != e; ++i) {
    if (llvm_struct_initialization[i]->getType() ==
        llvm_struct_type->getElementType(i)) {
      continue;
    } else {
      debug_above(
          7, log(log_error,
                 "llvm_struct_initialization[%d] mismatch is %s should be %s",
                 i, llvm_print(*llvm_struct_initialization[i]).c_str(),
                 llvm_print(llvm_struct_type->getElementType(i)).c_str()));
      assert(false);
    }
  }
}

llvm::GlobalVariable *llvm_get_global(llvm::Module *llvm_module,
                                      std::string name,
                                      llvm::Constant *llvm_constant,
                                      bool is_constant) {
  assert(llvm_module != nullptr);
  auto llvm_global_variable = new llvm::GlobalVariable(
      *llvm_module, llvm_constant->getType(), is_constant,
      llvm::GlobalValue::PrivateLinkage, llvm_constant, name, nullptr,
      llvm::GlobalVariable::NotThreadLocal);

  // llvm_global_variable->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  auto v = llvm_global_variable;
  debug_above(4, log("llvm_get_global(..., %s, %s, %s) -> %s", name.c_str(),
                     llvm_print(llvm_constant).c_str(), boolstr(is_constant),
                     llvm_print(v).c_str()));
  return v;
}

llvm::Value *llvm_maybe_pointer_cast(llvm::IRBuilder<> &builder,
                                     llvm::Value *llvm_value,
                                     llvm::Type *llvm_type) {
  if (llvm_value->getType() == llvm_type) {
    return llvm_value;
  }

  return builder.CreateBitOrPointerCast(llvm_value, llvm_type);
}

llvm::Value *llvm_int_cast(llvm::IRBuilder<> &builder,
                           llvm::Value *llvm_value,
                           llvm::Type *llvm_type) {
  return builder.CreateIntCast(llvm_value, llvm_type, false /*isSigned*/);
}

void explain(llvm::Type *llvm_type) {
  INDENT(6, string_format("explain %s", llvm_print(llvm_type).c_str()));

  if (auto llvm_struct_type = llvm::dyn_cast<llvm::StructType>(llvm_type)) {
    for (auto element : llvm_struct_type->elements()) {
      explain(element);
    }
  } else if (auto lp = llvm::dyn_cast<llvm::PointerType>(llvm_type)) {
    explain(lp->getElementType());
  }
}

llvm::Value *llvm_last_param(llvm::Function *llvm_function) {
  llvm::Value *last = nullptr;
  for (auto arg = llvm_function->arg_begin(); arg != llvm_function->arg_end();
       ++arg) {
    last = &*arg;
  }
  assert(last != nullptr);
  return last;
}

llvm::FunctionType *get_llvm_arrow_function_type(llvm::IRBuilder<> &builder,
                                                 const types::TypeEnv &type_env,
                                                 const types::Refs &terms) {
  std::vector<llvm::Type *> llvm_param_types;
  for (size_t i = 0; i < terms.size() - 1; ++i) {
    auto &term = terms[i];
    llvm_param_types.push_back(get_llvm_type(builder, type_env, term));
  }

  /* push the closure */
  llvm_param_types.push_back(builder.getInt8Ty()->getPointerTo());

  llvm::Type *return_type = get_llvm_type(builder, type_env, terms.back());

  /* get the llvm function type for the data ctor */
  return llvm::FunctionType::get(return_type,
                                 llvm::ArrayRef<llvm::Type *>(llvm_param_types),
                                 false /*isVarArg*/);
}

llvm::Type *get_llvm_closure_type(llvm::IRBuilder<> &builder,
                                  const types::TypeEnv &type_env,
                                  const types::Refs &terms) {
  return llvm::StructType::get(
             get_llvm_arrow_function_type(builder, type_env, terms)
                 ->getPointerTo(),
             builder.getInt8Ty()->getPointerTo())
      ->getPointerTo();
}

llvm::Type *get_llvm_type(llvm::IRBuilder<> &builder,
                          const types::TypeEnv &type_env,
                          const types::Ref &type_) {
  auto type = type_->eval(type_env);
  debug_above(3, log("get_llvm_type(%s)...", type->str().c_str()));
  if (auto id = dyncast<const types::TypeId>(type)) {
    const std::string &name = id->id.name;
    if (name == CHAR_TYPE) {
      return builder.getInt8Ty();
    } else if (name == INT_TYPE) {
      return builder.getZionIntTy();
    } else if (name == FLOAT_TYPE) {
      return builder.getDoubleTy();
    } else {
      return builder.getInt8Ty()->getPointerTo();
    }
  } else if (auto tuple_type = dyncast<const types::TypeTuple>(type)) {
    if (tuple_type->dimensions.size() == 0) {
      return builder.getInt8Ty()->getPointerTo();
    }
    std::vector<llvm::Type *> llvm_types = get_llvm_types(
        builder, type_env, tuple_type->dimensions);
    llvm::StructType *llvm_struct_type = llvm_create_struct_type(builder,
                                                                 llvm_types);
    return llvm_struct_type->getPointerTo();
  } else if (auto operator_ = dyncast<const types::TypeOperator>(type)) {
    if (types::is_type_id(operator_->oper, PTR_TYPE_OPERATOR)) {
      /* handle pointer types */
      return get_llvm_type(builder, type_env, operator_->operand)
          ->getPointerTo();
    } else {
      types::Refs terms = unfold_arrows(type);
      if (terms.size() == 1) {
        /* user defined types are recast at their usage site, not passed around
         * structurally */
        return builder.getInt8Ty()->getPointerTo();
      } else {
        assert(terms.size() > 1);
        return get_llvm_closure_type(builder, type_env, terms);
      }
    }
  } else if (auto variable = dyncast<const types::TypeVariable>(type)) {
    assert(false);
    return nullptr;
  } else if (auto lambda = dyncast<const types::TypeLambda>(type)) {
    assert(false);
    return nullptr;
  } else {
    assert(false);
    return nullptr;
  }
}

std::vector<llvm::Type *> get_llvm_types(llvm::IRBuilder<> &builder,
                                         const types::TypeEnv &type_env,
                                         const types::Refs &types) {
  debug_above(7, log("get_llvm_types([%s])...", join_str(types, ", ").c_str()));
  std::vector<llvm::Type *> llvm_types;
  for (auto type : types) {
    llvm_types.push_back(get_llvm_type(builder, type_env, type));
  }
  return llvm_types;
}

std::vector<llvm::Type *> llvm_get_types(
    const std::vector<llvm::Value *> &llvm_values) {
  std::vector<llvm::Type *> llvm_types;
  for (auto llvm_value : llvm_values) {
    llvm_types.push_back(llvm_value->getType());
  }
  return llvm_types;
}

llvm::Value *llvm_tuple_alloc(llvm::IRBuilder<> &builder,
                              llvm::Module *llvm_module,
                              const std::vector<llvm::Value *> llvm_dims) {
  if (llvm_dims.size() == 0) {
    return llvm::Constant::getNullValue(builder.getInt8Ty()->getPointerTo());
  }

  llvm::StructType *llvm_tuple_type = llvm_create_struct_type(builder,
                                                              llvm_dims);
  if (builder.GetInsertBlock() == nullptr) {
    /* we are at global scope, so let's not allocate */
    debug_above(6, log("creating a constant tuple of type %s at global scope",
                       llvm_print(llvm_tuple_type).c_str()));
    std::vector<llvm::Constant *> llvm_const_dims;
    llvm_const_dims.reserve(llvm_dims.size());
    for (auto llvm_dim : llvm_dims) {
      llvm_const_dims.push_back(llvm::dyn_cast<llvm::Constant>(llvm_dim));
    }

    return llvm_get_global(
        llvm_module, "tuple",
        llvm::ConstantStruct::get(llvm_tuple_type, llvm_const_dims),
        true /*is_constant*/);
  } else {
    assert(llvm_module == llvm_get_module(builder));

    llvm::Type *alloc_terms[] = {builder.getInt64Ty()};
    debug_above(6, log("need to allocate a tuple of type %s",
                       llvm_print(llvm_tuple_type).c_str()));
    auto llvm_alloc_func_decl = llvm::cast<llvm::Function>(
        llvm_module->getOrInsertFunction(
            "zion_malloc",
            llvm::FunctionType::get(builder.getInt8Ty()->getPointerTo(),
                                    alloc_terms, false /*isVarArg*/)));
    llvm::Value *llvm_allocated_tuple = builder.CreateBitCast(
        builder.CreateCall(llvm_alloc_func_decl,
                           std::vector<llvm::Value *>{
                               llvm_sizeof_type(builder, llvm_tuple_type)}),
        llvm_tuple_type->getPointerTo());
    llvm_allocated_tuple->setName(
        string_format("tuple/%d", int(llvm_dims.size())));

    llvm::Value *llvm_zero = llvm::ConstantInt::get(
        llvm::Type::getInt32Ty(builder.getContext()), 0);

    /* actually copy the dims into the allocated space */
    for (size_t i = 0; i < llvm_dims.size(); ++i) {
      llvm::Value *llvm_index = llvm::ConstantInt::get(
          llvm::Type::getInt32Ty(builder.getContext()), i);
      llvm::Value *llvm_gep_args[] = {llvm_zero, llvm_index};
      debug_above(7,
                  log("builder.CreateStore(%s, builder.CreateInBoundsGEP(%s, "
                      "%s, {0, %d}))",
                      llvm_print(llvm_dims[i]->getType()).c_str(),
                      llvm_print(llvm_tuple_type).c_str(),
                      llvm_print(llvm_allocated_tuple->getType()).c_str(), i));
      llvm::Value *llvm_member_address = builder.CreateInBoundsGEP(
          llvm_tuple_type, llvm_allocated_tuple, llvm_gep_args);
      llvm_member_address->setName(
          string_format("&tuple/%d[%d]", int(llvm_dims.size()), i));
      debug_above(7, log("GEP returned %s with type %s",
                         llvm_print(llvm_member_address).c_str(),
                         llvm_print(llvm_member_address->getType()).c_str()));

      builder.CreateStore(
          builder.CreateBitCast(
              llvm_dims[i],
              llvm_member_address->getType()->getPointerElementType(),
              "to.be.stored"),
          llvm_member_address);
    }
    return llvm_allocated_tuple;
  }
}

void destructure_closure(llvm::IRBuilder<> &builder,
                         llvm::Value *closure,
                         llvm::Value **llvm_function,
                         llvm::Value **llvm_closure_env) {
#ifdef ZION_DEBUG
  assert(closure->getType()->isPointerTy());
  auto inner_type = closure->getType()->getPointerElementType();
  auto struct_type = llvm::dyn_cast<llvm::StructType>(inner_type);
  assert(struct_type != nullptr);
  assert(struct_type->getNumElements() == 2);

  // First part of the tuple is the function pointer
  auto llvm_function_ptr_type = struct_type->getElementType(0);
  assert(llvm_function_ptr_type->isPointerTy());
  auto llvm_callable_type = llvm_function_ptr_type->getPointerElementType();
  assert(llvm_callable_type != nullptr);
  llvm::FunctionType *llvm_function_type = llvm::dyn_cast<llvm::FunctionType>(
      llvm_callable_type);
  assert(llvm_function_type != nullptr);

  auto params = llvm_function_type->params();

  // Must accept the closure env.
  assert(params.size() >= 1);
  assert(params.back() == builder.getInt8Ty()->getPointerTo());

  // Second part of the tuple is the closure env pointer (i8*)
  assert(struct_type->getElementType(1) == builder.getInt8Ty()->getPointerTo());
#endif

  llvm::Value *gep_function_path[] = {builder.getInt32(0), builder.getInt32(0)};
  llvm::Value *gep_env_path[] = {builder.getInt32(0), builder.getInt32(1)};

  if (llvm_function != nullptr) {
    *llvm_function = builder.CreateLoad(builder.CreateInBoundsGEP(
        closure, llvm::ArrayRef<llvm::Value *>(gep_function_path)));
#ifdef ZION_DEBUG
    if (llvm_function_type !=
        (*llvm_function)->getType()->getPointerElementType()) {
      log("why does %s != %s", llvm_print(llvm_function_type).c_str(),
          llvm_print((*llvm_function)->getType()).c_str());
      dbg();
    }
#endif
  }
  if (llvm_closure_env != nullptr) {
    *llvm_closure_env = builder.CreateLoad(builder.CreateInBoundsGEP(
        closure, llvm::ArrayRef<llvm::Value *>(gep_env_path)));
  }
  dbg_when(llvm_print(*llvm_function).find("badref") != std::string::npos);
}

llvm::Value *llvm_create_closure_callsite(Location location,
                                          llvm::IRBuilder<> &builder,
                                          llvm::Value *closure,
                                          std::vector<llvm::Value *> args) {
  llvm::Value *llvm_function_to_call = nullptr;
  destructure_closure(builder, closure, &llvm_function_to_call, nullptr);

  args.push_back(builder.CreateBitCast(
      closure, builder.getInt8Ty()->getPointerTo(), "closure_cast"));

  debug_above(4, log("calling builder.CreateCall(%s, {%s, %s})",
                     llvm_print(llvm_function_to_call->getType()).c_str(),
                     llvm_print(args[0]->getType()).c_str(),
                     llvm_print(args[1]->getType()).c_str()));
  return builder.CreateCall(
      llvm_function_to_call, llvm::ArrayRef<llvm::Value *>(args),
      string_format("call{%s}", location.repr().c_str()));
}

} // namespace zion
