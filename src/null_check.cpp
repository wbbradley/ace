#include "zion.h"
#include "null_check.h"
#include "ast.h"
#include "bound_var.h"
#include "logger.h"
#include "compiler.h"
#include "llvm_types.h"
#include "code_id.h"

bound_var_t::ref resolve_null_check(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		const std::vector<ptr<ast::expression_t>> &params,
		null_check_kind_t nck)
{
	// REVIEW: when does this happen?
	assert(false);
	if (params.size() != 1) {
		user_error(status, location, "null checks may only have one parameter");
	}

	if (!!status) {
		auto param = params[0];
		bound_var_t::ref param_var = param->resolve_expression(
				status, builder, scope, life, false /*as_ref*/);

		if (!!status) {
			return resolve_null_check(status, builder, scope, life, location, nullptr, param_var, nck, nullptr);
		}
	}

	assert(!status);
	return nullptr;
}

void unmaybe_variable(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		ast::reference_expr_t::ref ref_expr,
		local_scope_t::ref *new_scope)
{
	token_t token = ref_expr->token;
	bound_var_t::ref var = scope->get_bound_variable(status, ref_expr->get_location(), token.text);

	if (!!status) {
		if (var == nullptr) {
			user_error(status, ref_expr->get_location(), "undefined symbol " c_id("%s"), token.text.c_str());
		}

		bool was_ref = false;
		types::type_t::ref type = var->type->get_type();
		if (auto ref_type = dyncast<const types::type_ref_t>(type)) {
			was_ref = true;
			type = ref_type->element_type;
		}

		if (auto maybe_type = dyncast<const types::type_maybe_t>(type)) {
			runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
			assert(runnable_scope != nullptr);

			/* variable declarations begin new scopes */
			local_scope_t::ref fresh_scope = runnable_scope->new_local_scope(
					string_format("unmaybe-%s", token.text.c_str()));

			scope = fresh_scope;
			*new_scope = fresh_scope;

			auto bound_type = upsert_bound_type(status, builder, scope,
					was_ref ? type_ref(maybe_type->just) : maybe_type->just);

			if (!!status) {
				// TODO: decide whether this variable can be made into a ref
				// type if "was_ref" is true, to enable reassignment. The
				// downfall of this is that even if this is allowed, then a null
				// value can never be assigned to this value. This could
				// generally be considered good in the abstract, however in
				// practice it's common for users to want to assign variables a
				// null value as a way of signaling loop exits and the like.

				/* because we're evaluating this maybe value in the context of a
				 * condition (super simplified at this point), let's redeclare it
				 * without its maybe, since we know it will be valid if the
				 * condition passes */
				bound_var_t::ref var_decl_variable =
					bound_var_t::create(INTERNAL_LOC(), token.text, bound_type,
							var->get_llvm_value(), make_code_id(token));

				/* on our way out, stash the variable in the current scope */
				scope->put_bound_variable(status, var_decl_variable->name,
						var_decl_variable);
				return;
			}
		} else {
			/* this is not a maybe, so let's just move along */
			return;
		}
	}

	assert(!status);
	return;
}

bound_var_t::ref resolve_null_check(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		location_t location,
		ast::expression_t::ref node,
		bound_var_t::ref value,
		null_check_kind_t nck,
		local_scope_t::ref *new_scope)
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
		if (new_scope != nullptr) {
			if (auto ref_expr = dyncast<const ast::reference_expr_t>(node)) {
				unmaybe_variable(status, builder, scope, life, ref_expr, new_scope);
			}
		}
		break;
	case nck_is_null:
		llvm_bool_value = builder.CreateIntCast(
				builder.CreateICmpEQ(llvm_value, zero),
				llvm_bool_type, false /*isSigned*/);
		break;
	}

	if (!!status) {
		assert(llvm_bool_value != nullptr);
		return bound_var_t::create(
				INTERNAL_LOC(), "nullcheck",
				bound_bool_type, llvm_bool_value, make_iid("nullcheck"));
	}

	assert(!status);
	return nullptr;
}
