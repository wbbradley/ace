#include "zion.h"
#include "llvm_types.h"
#include "status.h"
#include "scopes.h"
#include "types.h"
#include "bound_var.h"
#include "dbg.h"

llvm::Value *coerce_value(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		types::type_t::ref lhs_type,
		bound_var_t::ref rhs)
{
	auto bound_lhs_type = upsert_bound_type(status, builder, scope, lhs_type);
	if (!!status) {
		if (!lhs_type->is_ref()) {
			/* make sure that if the lhs is not a ref, we don't pass a ref */
			rhs = rhs->resolve_bound_value(status, builder, scope);
		} else {
			// I thought we weren't supporting this!?
			assert(false);
		}

		if (!!status) {
			auto rhs_type = rhs->type->get_type();
			assert(!rhs_type->is_ref());

			/* get the target type */
			llvm::Type *llvm_lhs_type = bound_lhs_type->get_llvm_type();

			/* get the incoming value and its current type */
			llvm::Value *llvm_rhs_value = rhs->get_llvm_value();
			llvm::Type *llvm_rhs_type = llvm_rhs_value->getType();

			if (llvm_lhs_type == llvm_rhs_type) {
				/* types are the same, we're done */
				return llvm_rhs_value;
			}

			/* There is some coupling here with unification, since we'll need to make these
			 * compatible. Nevertheless, if we are here, then that means we must try to make the
			 * rhs type become the lhs type. */
			debug_above(5, log(log_info, "seeing about coercion from %s (aka %s) to %s (aka %s)",
						rhs->type->str().c_str(),
						llvm_print(llvm_rhs_value).c_str(),
						lhs_type->str().c_str(),
						llvm_print(llvm_lhs_type).c_str()));

			/* check pragmatically for certain coercions that should take place */
			if (llvm_lhs_type->isIntegerTy() && llvm_rhs_type->isIntegerTy()) {
				/* automatically resize integers to match the lhs */
				unsigned bit_size = 0;
				bool signed_ = false;
				types::get_integer_attributes(status, rhs->type->get_type(), scope->get_typename_env(), bit_size, signed_);
				if (!!status) {
					if (signed_) {
						return builder.CreateSExtOrTrunc(llvm_rhs_value, llvm_lhs_type);
					} else {
						return builder.CreateZExtOrTrunc(llvm_rhs_value, llvm_lhs_type);
					}
				}
			} else if (rhs->type->get_type()->is_nil()) {
				/* we're passing in a null value */
				assert(llvm_lhs_type->isPointerTy());
				return llvm::Constant::getNullValue(llvm_lhs_type);
			} else if (llvm_lhs_type->isPointerTy() && llvm_rhs_type->isPointerTy()) {
				return builder.CreateBitCast(llvm_rhs_value, llvm_lhs_type);
			} else {
				debug_above(2, log(log_info, "probably need to write some smarter coercion code"));
				assert(false);
				dbg();
				return rhs->get_llvm_value();
			}
		}
	}
	assert(!status);
	return nullptr;
}

std::vector<llvm::Value *> get_llvm_values(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		ptr<const types::type_args_t> type_args,
		const bound_var_t::refs &vars)
{
	std::vector<llvm::Value *> llvm_values;
	llvm_values.reserve(vars.size());

	if (type_args->args.size() != vars.size()) {
		user_error(status, location, "invalid parameter count to function call. expected %d parameters, got %d",
				(int)type_args->args.size(),
				(int)vars.size());
		return {};
	}

	auto type_iter = type_args->args.begin();
	for (size_t i = 0; i < vars.size() && !!status; ++i, ++type_iter) {
		auto rhs = vars[i];
		llvm::Value *llvm_value = coerce_value(status, builder, scope, location, *type_iter, rhs);
		if (!!status) {
			llvm_values.push_back(llvm_value);
		}
	}

	if (!!status) {
		return llvm_values;
	}

	assert(!status);
	return {};
}

