#include "zion.h"
#include "nil_check.h"
#include "ast.h"
#include "bound_var.h"
#include "logger.h"
#include "compiler.h"

bound_var_t::ref resolve_nil_check(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		const std::vector<ptr<ast::expression_t>> &params,
		nil_check_kind_t nck)
{
	if (params.size() != 1) {
		user_error(status, location, "nil checks may only have one parameter");
	}

	if (!!status) {
		auto param = params[0];
		bound_var_t::ref param_var = param->resolve_expression(
				status, builder, scope, life, false /*as_ref*/);

		if (!!status) {
			return resolve_nil_check(status, builder, scope, life, location, param_var, nck);
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref resolve_nil_check(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		bound_var_t::ref value,
		nil_check_kind_t nck)
{
	assert(value->is_pointer());
	llvm::Value *llvm_value = value->resolve_bound_var_value(builder);
	if (llvm::dyn_cast<llvm::PointerType>(llvm_value->getType())) {
		llvm::Value *llvm_bool_value;

		bound_type_t::ref bound_bool_type = scope->get_bound_type(BOOL_TYPE);
		llvm::Type *llvm_bool_type = bound_bool_type->get_llvm_specific_type();

		switch (nck) {
		case nck_is_non_nil:
			llvm_bool_value = builder.CreateICmpNE(llvm_value,
					llvm::Constant::getNullValue(llvm_value->getType()));
			llvm_bool_value = builder.CreateIntCast(llvm_bool_value, llvm_bool_type, false /*isSigned*/);
			break;
		case nck_is_nil:
			llvm_bool_value = builder.CreateICmpEQ(llvm_value,
					llvm::Constant::getNullValue(llvm_value->getType()));
			llvm_bool_value = builder.CreateIntCast(llvm_bool_value, llvm_bool_type, false /*isSigned*/);
			break;
		}

		return bound_var_t::create(
				INTERNAL_LOC(), "nilcheck",
				bound_bool_type, llvm_bool_value, make_iid("nilcheck"));
	} else {
		user_error(status, location, "cannot check for nil for value of type %s (may just not be implemented yet)",
				value->type->str().c_str());
	}

	assert(!status);
	return nullptr;
}
