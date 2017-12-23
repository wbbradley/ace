#include "zion.h"
#include "null_check.h"
#include "ast.h"
#include "bound_var.h"
#include "logger.h"
#include "compiler.h"

bound_var_t::ref resolve_null_check(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		const std::vector<ptr<ast::expression_t>> &params,
		null_check_kind_t nck)
{
	if (params.size() != 1) {
		user_error(status, location, "null checks may only have one parameter");
	}

	if (!!status) {
		auto param = params[0];
		bound_var_t::ref param_var = param->resolve_expression(
				status, builder, scope, life, false /*as_ref*/);

		if (!!status) {
			return resolve_null_check(status, builder, scope, life, location, param_var, nck);
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref resolve_null_check(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		bound_var_t::ref value,
		null_check_kind_t nck)
{
	llvm::Value *llvm_value = value->resolve_bound_var_value(builder);
	bound_type_t::ref bound_bool_type = scope->get_bound_type(BOOL_TYPE);
	llvm::Type *llvm_bool_type = bound_bool_type->get_llvm_specific_type();

	llvm::Constant *zero;
	if (llvm::dyn_cast<llvm::PointerType>(llvm_value->getType())) {
		zero = llvm::Constant::getNullValue(llvm_value->getType());
	} else if (llvm_value->getType()->isIntegerTy()) {
		zero = llvm::ConstantInt::get(llvm_value->getType(), 0);
	} else {
		assert(false);
	}

	llvm::Value *llvm_bool_value;
	switch (nck) {
	case nck_is_non_null:
		llvm_bool_value = builder.CreateIntCast(
				builder.CreateICmpNE(llvm_value, zero),
				llvm_bool_type, false /*isSigned*/);
		break;
	case nck_is_null:
		llvm_bool_value = builder.CreateIntCast(
				builder.CreateICmpEQ(llvm_value, zero),
				llvm_bool_type, false /*isSigned*/);
		break;
	}
	assert(llvm_bool_value != nullptr);
	return bound_var_t::create(
			INTERNAL_LOC(), "nullcheck",
			bound_bool_type, llvm_bool_value, make_iid("nullcheck"));
}
