#include "zion.h"
#include "llvm_types.h"
#include "user_error.h"
#include "scopes.h"
#include "types.h"
#include "bound_var.h"
#include "dbg.h"
#include "unification.h"
#include "coercions.h"
#include "type_checker.h"

bound_var_t::ref coerce_bound_value(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		types::type_t::ref lhs_type,
		bound_var_t::ref rhs)
{
	bound_type_t::ref bound_type = upsert_bound_type(builder, scope, lhs_type);
	auto llvm_value = coerce_value(builder, scope, life, location, lhs_type, rhs);
	return bound_var_t::create(
			INTERNAL_LOC(),
			"coerced.value",
			bound_type,
			llvm_value,
			make_iid("coerced.value"));
}

llvm::Value *coerce_value(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		types::type_t::ref lhs_type,
		bound_var_t::ref rhs)
{
	static int depth = 0;
	depth_guard_t depth_guard(location, depth, 3);

	lhs_type = lhs_type->eval(scope);

	if (auto maybe_type = dyncast<const types::type_maybe_t>(lhs_type)) {
		/* forget about maybe checking, since we've done that prior to this */
		lhs_type = maybe_type->just;
	}

	if (!lhs_type->eval_predicate(tb_ref, scope)) {
		/* make sure that if the lhs is not a ref, we don't pass a ref */
		rhs = rhs->resolve_bound_value(builder, scope);
	} else {
		// I thought we weren't supporting this!?
		assert(false);
	}

	assert(!rhs->type->is_bottom(scope));

	auto rhs_type = rhs->type->get_type();

	auto bound_lhs_type = upsert_bound_type(builder, scope, lhs_type);
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
	if (lhs_type->eval_predicate(tb_false, scope)) {
		return llvm::ConstantInt::get(
				bound_lhs_type->get_llvm_specific_type(), 0, false);
	} else if (lhs_type->eval_predicate(tb_true, scope)) {
		return llvm::ConstantInt::get(
				bound_lhs_type->get_llvm_specific_type(), 1, false);
	} else if (lhs_type->eval_predicate(tb_null, scope) || rhs_type->eval_predicate(tb_null, scope)) {
		return llvm::Constant::getNullValue(llvm_lhs_type);
	}

	assert(!rhs_type->eval_predicate(tb_ref, scope));

	/* get the incoming value and its current type */
	llvm::Type *llvm_rhs_type = llvm_rhs_value->getType();

	if (llvm_lhs_type == llvm_rhs_type) {
		/* types are the same, we're done */
		return llvm_rhs_value;
	}

	/* check pragmatically for certain coercions that should take place */
	bool lhs_is_managed = types::is_managed_ptr(lhs_type, scope);
	bool rhs_is_managed = types::is_managed_ptr(rhs_type, scope);
	if (lhs_is_managed && rhs_is_managed) {
		/* we must trust the type system! */
		debug_above(6, log("casting a %s to be a %s", rhs_type->str().c_str(), lhs_type->str().c_str()));
		return builder.CreateBitCast(llvm_rhs_value, llvm_lhs_type);
	} else if (lhs_is_managed) {
		debug_above(6, log(log_info, "calling " c_id("__box__") " on %s to try to get a %s", rhs_type->str().c_str(), lhs_type->str().c_str()));
		bound_var_t::ref coercion = call_program_function(
				builder, scope, life,
				"__box__", location, {rhs}, lhs_type);

		/* trust the type system. */
		return builder.CreateBitCast(coercion->get_llvm_value(), llvm_lhs_type);
	} else if (rhs_is_managed) {
		if (types::is_ptr_type_id(lhs_type, STD_MANAGED_TYPE, scope)) {
			// Consider allowing this conversion for all pointers, since coercion is
			// fierce in what it will do, why even check? We'll wait a bit on that. It
			// may become apparent that unboxing managed objects into a native pointer
			// type makes sense somehow...

			/* the *var_t object accepts all managed objects */
			return builder.CreateBitCast(llvm_rhs_value, llvm_lhs_type);
		} else if (types::is_ptr_type_id(lhs_type, CHAR_TYPE, scope) &&
			   	types::is_type_id(rhs_type, MANAGED_STR, scope)) {
			/* custom unboxing because we need to inject some life management to cleanup in the
			 * event that we need to convert a slice to a heap-alloced null-terminated string. */
			bound_var_t::ref c_str = call_program_function(
					builder, scope, life,
					"c_str", location, {rhs}, nullptr);

			/* c_str is now an OwningBuffer */
			auto raw_c_str = extract_member_variable(
				builder, scope, life,
				location,
				c_str,
				"raw", // OwningBuffer.raw
				false /* as_ref */,
				type_ptr(type_id(make_iid_impl(CHAR_TYPE, INTERNAL_LOC()))));
			return raw_c_str->get_llvm_value();
		} else {
			throw user_error(rhs->get_location(), "unsure how to get native value of type %s from managed value of type %s [%s, %s]",
					lhs_type->str().c_str(),
					rhs_type->str().c_str(),
					boolstr(lhs_is_managed),
					boolstr(rhs_is_managed));
		}
	} else {
		debug_above(7, log("trying to coerce native value %s to %s", llvm_print(llvm_rhs_value).c_str(), llvm_print(llvm_lhs_type).c_str()));
		if (llvm_lhs_type->isPointerTy() && llvm_rhs_type->isPointerTy()) {
			return builder.CreateBitCast(llvm_rhs_value, llvm_lhs_type);
		}

		if (llvm_lhs_type->isIntegerTy() && llvm_rhs_type->isIntegerTy()) {
			/* automatically resize integers to match the lhs */
			unsigned bit_size = 0;
			bool signed_ = false;
			if (types::maybe_get_integer_attributes(rhs->get_location(), rhs->type->get_type(), scope, bit_size, signed_)) {
				if (signed_) {
					return builder.CreateSExtOrTrunc(llvm_rhs_value, llvm_lhs_type);
				} else {
					return builder.CreateZExtOrTrunc(llvm_rhs_value, llvm_lhs_type);
				}
			} else {
				throw user_error(rhs->get_location(), "could not determine integer makeup for %s",
						rhs->str().c_str());
				return nullptr;
			}
		}

		if (rhs->type->get_type()->eval_predicate(tb_null, scope)) {
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

std::vector<llvm::Value *> get_llvm_values(
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
		dbg();
		auto error = user_error(location, "invalid parameter count to function call. expected %d parameters, got %d",
				(int)type_args->args.size(),
				(int)vars.size());
		error.add_info(type_args->get_location(), "target args is %s",
				type_args->str().c_str());
		error.add_info(location, "input args is [%s]",
				join_with(vars, ", ", [] (bound_var_t::ref var) -> std::string { return var->str(); }).c_str());
		throw error;
	}

	auto type_iter = type_args->args.begin();
	for (size_t i = 0; i < vars.size(); ++i, ++type_iter) {
		auto rhs = vars[i];
		llvm::Value *llvm_value = coerce_value(builder, scope, life, location, *type_iter, rhs);
		llvm_values.push_back(llvm_value);
	}

	return llvm_values;
}

