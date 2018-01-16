#include "zion.h"
#include "llvm_types.h"
#include "status.h"
#include "scopes.h"
#include "types.h"
#include "bound_var.h"
#include "dbg.h"
#include "unification.h"

llvm::Value *coerce_value(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		types::type_t::ref lhs_type,
		bound_var_t::ref rhs)
{
	if (!lhs_type->is_ref()) {
		/* make sure that if the lhs is not a ref, we don't pass a ref */
		rhs = rhs->resolve_bound_value(status, builder, scope);
	} else {
		// I thought we weren't supporting this!?
		assert(false);
	}

	auto rhs_type = rhs->type->get_type();
	auto bound_lhs_type = upsert_bound_type(status, builder, scope, lhs_type);
	if (!!status) {
		llvm::Value *llvm_rhs_value = rhs->get_llvm_value();

		/* get the target type */
		llvm::Type *llvm_lhs_type = bound_lhs_type->get_llvm_type();

		/* There is some coupling here with unification, since we'll need to make these
		 * compatible. Nevertheless, if we are here, then that means we must try to make the
		 * rhs type become the lhs type. */
		debug_above(5, log(log_info, "seeing about coercion from %s (aka %s) to %s (aka %s)",
					rhs->type->str().c_str(),
					llvm_print(llvm_rhs_value).c_str(),
					lhs_type->str().c_str(),
					llvm_print(llvm_lhs_type).c_str()));

		/* handle some cases where we can just pass constants back */
		if (lhs_type->is_false() || lhs_type->is_zero()) {
			return llvm::ConstantInt::get(
					bound_lhs_type->get_llvm_specific_type(), 0, false);
		} else if (lhs_type->is_true()) {
			return llvm::ConstantInt::get(
					bound_lhs_type->get_llvm_specific_type(), 1, false);
		} else if (lhs_type->is_null() || (lhs_type->is_maybe() && rhs_type->is_null())) {
			return llvm::Constant::getNullValue(llvm_lhs_type);
		}

		assert(!rhs_type->is_ref());

		/* get the incoming value and its current type */
		llvm::Type *llvm_rhs_type = llvm_rhs_value->getType();

		if (llvm_lhs_type == llvm_rhs_type) {
			/* types are the same, we're done */
			return llvm_rhs_value;
		}

		/* check pragmatically for certain coercions that should take place */
		bool lhs_is_managed = false;
		bound_lhs_type->is_managed_ptr(
				status,
				builder,
				scope,
				lhs_is_managed);
		if (!!status) {
			bool rhs_is_managed = false;
			rhs->type->is_managed_ptr(
					status,
					builder,
					scope,
					rhs_is_managed);

			if (!!status) {
				if (lhs_is_managed && rhs_is_managed) {
					/* we must trust the type system! */
					debug_above(6, log("casting a %s to be a %s", rhs_type->str().c_str(), lhs_type->str().c_str()));
					return builder.CreateBitCast(llvm_rhs_value, llvm_lhs_type);
				} else if (lhs_is_managed) {
					if (rhs_type->is_zero()) {
						/* let's elevate zero to the managed version of itself */
						bound_var_t::ref bound_Zero = scope->get_program_scope()->get_bound_variable(status, location, "Zero", false);
						if (!!status) {
							/* trust the type system */
							return builder.CreateBitCast(
									bound_Zero->resolve_bound_var_value(builder), llvm_lhs_type);
						}
					} else {
						debug_above(6, log(log_info, "calling " c_id("__box__") " on %s to try to get a %s", rhs_type->str().c_str(), lhs_type->str().c_str()));
						bound_var_t::ref coercion = call_program_function(
								status, builder, scope, life,
								"__box__", location, {rhs});

						if (!!status) {
							/* trust the type system. */
							return builder.CreateBitCast(coercion->get_llvm_value(), llvm_lhs_type);
						}
					}
				} else if (rhs_is_managed) {
					debug_above(6, log(log_info, "calling " c_id("__unbox__") " on %s to try to get a %s", rhs_type->str().c_str(), lhs_type->str().c_str()));
					bound_var_t::ref coercion = call_program_function(
							status, builder, scope, life,
							"__unbox__", location, {rhs}, lhs_type);

					if (!!status) {
						return coercion->get_llvm_value();
					}
				} else {
					if (llvm_lhs_type->isPointerTy() && llvm_rhs_type->isPointerTy()) {
						return builder.CreateBitCast(llvm_rhs_value, llvm_lhs_type);
					}

					if (llvm_lhs_type->isIntegerTy() && llvm_rhs_type->isIntegerTy()) {
						/* automatically resize integers to match the lhs */
						unsigned bit_size = 0;
						bool signed_ = false;
						types::get_integer_attributes(status, rhs->type->get_type(), scope->get_total_env(), bit_size, signed_);
						if (!!status) {
							if (signed_) {
								return builder.CreateSExtOrTrunc(llvm_rhs_value, llvm_lhs_type);
							} else {
								return builder.CreateZExtOrTrunc(llvm_rhs_value, llvm_lhs_type);
							}
						}
					} else if (rhs->type->get_type()->is_null()) {
						/* we're passing in a null value */
						assert(llvm_lhs_type->isPointerTy());
						return llvm::Constant::getNullValue(llvm_lhs_type);
					}
					log(log_info, "missing coercion of %s to %s", rhs_type->str().c_str(),
							lhs_type->str().c_str());
					assert(false);
					dbg();
					return rhs->get_llvm_value();
				}
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
		life_t::ref life,
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
		llvm::Value *llvm_value = coerce_value(status, builder, scope, life, location, *type_iter, rhs);
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

