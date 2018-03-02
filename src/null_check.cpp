#include "zion.h"
#include "null_check.h"
#include "ast.h"
#include "bound_var.h"
#include "logger.h"
#include "compiler.h"
#include "llvm_types.h"
#include "code_id.h"

bound_var_t::ref get_null(
        status_t &status,
        llvm::IRBuilder<> &builder,
        scope_t::ref scope,
		location_t location)
{
	auto program_scope = scope->get_program_scope();
	auto null_type = program_scope->get_bound_type("null");
	auto bound_type = bound_type_t::create(
			type_null(),
			location,
			null_type->get_llvm_type(),
			null_type->get_llvm_specific_type());
	return bound_var_t::create(
			INTERNAL_LOC(), "null", bound_type,
			llvm::Constant::getNullValue(null_type->get_llvm_specific_type()),
			make_iid_impl("null", location));
}

#if 0
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
			return resolve_null_check(
					status, builder, scope, life, location, nullptr, param_var,
					nck, nullptr, nullptr);
		}
	}

	assert(!status);
	return nullptr;
}
#endif

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

void nullify_let_var(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		ast::reference_expr_t::ref ref_expr,
		local_scope_t::ref *new_scope)
{
	/* this is immutable so we can safely just refine it to null */
	token_t token = ref_expr->token;
	bound_var_t::ref var = scope->get_bound_variable(status, ref_expr->get_location(), token.text);
	types::type_t::ref type = var->type->get_type();

	if (!!status) {
		if (var == nullptr) {
			user_error(status, ref_expr->get_location(), "undefined symbol " c_id("%s"), token.text.c_str());
		}

		if (var->type->is_ref(scope)) {
			/* we can't change the type of a mutable name in the scope because the user is allowed
			 * to assign a non-null value, and we don't want to take that away from them */
			return;
		}

		if (auto maybe_type = dyncast<const types::type_maybe_t>(type)) {
			runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
			assert(runnable_scope != nullptr);

			/* variable declarations begin new scopes */
			local_scope_t::ref fresh_scope = runnable_scope->new_local_scope(
					string_format("nullify-%s", token.text.c_str()));

			scope = fresh_scope;
			*new_scope = fresh_scope;

			scope->put_bound_variable(status, token.text,
					get_null(status, builder, scope, ref_expr->get_location()));
			return;
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
		local_scope_t::ref *scope_if_true,
		local_scope_t::ref *scope_if_false)
{
	if (!value->type->is_maybe(scope) && value->type->is_ptr(scope)) {
		user_error(status, location, "%s cannot be null here (unless something unsafe happened), so this comparison is unnecessary. "
				"if you must compare this to null, try casting it to *?void first.",
				node->str().c_str());
		user_info(status, location, "the type of %s is %s", node->str().c_str(), value->type->str().c_str());
	} else {
		llvm::Value *llvm_value = value->resolve_bound_var_value(scope, builder);
		bound_type_t::ref bound_bool_type = upsert_bound_type(status, builder, scope, type_id(make_iid(BOOL_TYPE)));
		assert(!!status);
		assert(bound_bool_type != nullptr);
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

			if (auto ref_expr = dyncast<const ast::reference_expr_t>(node)) {
				if (scope_if_true != nullptr) {
					unmaybe_variable(status, builder, scope, life, ref_expr, scope_if_true);
				}
				if (scope_if_false != nullptr) {
					nullify_let_var(status, builder, scope, life, ref_expr, scope_if_false);
				}
			}
			break;
		case nck_is_null:
			llvm_bool_value = builder.CreateIntCast(
					builder.CreateICmpEQ(llvm_value, zero),
					llvm_bool_type, false /*isSigned*/);

			if (auto ref_expr = dyncast<const ast::reference_expr_t>(node)) {
				if (scope_if_false != nullptr) {
					unmaybe_variable(status, builder, scope, life, ref_expr, scope_if_false);
				}
				if (scope_if_true != nullptr) {
					nullify_let_var(status, builder, scope, life, ref_expr, scope_if_true);
				}
			}
			break;
		}

		if (!!status) {
			assert(llvm_bool_value != nullptr);
			return bound_var_t::create(
					INTERNAL_LOC(), "nullcheck",
					bound_bool_type, llvm_bool_value, make_iid("nullcheck"));
		}
	}

	assert(!status);
	return nullptr;
}
