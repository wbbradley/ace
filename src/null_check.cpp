#include "zion.h"
#include "null_check.h"
#include "ast.h"
#include "bound_var.h"
#include "logger.h"
#include "compiler.h"
#include "llvm_types.h"
#include "code_id.h"
#include "atom.h"
#include "patterns.h"

bound_var_t::ref get_null(
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

void unmaybe_variable(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		ast::reference_expr_t::ref ref_expr,
		runnable_scope_t::ref *new_scope)
{
	token_t token = ref_expr->token;
	bound_var_t::ref var = scope->get_bound_variable(builder, ref_expr->get_location(), token.text);

	if (var == nullptr) {
		throw user_error(ref_expr->get_location(), "undefined symbol " c_id("%s"), token.text.c_str());
	}

	bool was_ref = false;
	types::type_t::ref type = var->type->get_type()->eval(scope);
	if (auto ref_type = dyncast<const types::type_ref_t>(type)) {
		was_ref = true;
		type = ref_type->element_type;
	}

	if (auto maybe_type = dyncast<const types::type_maybe_t>(type)) {
		runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
		assert(runnable_scope != nullptr);

		runnable_scope_t::ref fresh_scope = runnable_scope->new_runnable_scope(
				string_format("unmaybe-%s", token.text.c_str()));

		scope = fresh_scope;
		*new_scope = fresh_scope;

		auto bound_type = upsert_bound_type(builder, scope,
				was_ref ? type_ref(maybe_type->just) : maybe_type->just);

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
		scope->put_bound_variable(var_decl_variable->name,
				var_decl_variable);
	} else {
		/* this is not a maybe, so let's just move along */
	}
}

void nullify_let_var(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		ast::reference_expr_t::ref ref_expr,
		runnable_scope_t::ref *new_scope)
{
	/* this is immutable so we can safely just refine it to null */
	token_t token = ref_expr->token;
	bound_var_t::ref var = scope->get_bound_variable(builder, ref_expr->get_location(), token.text);

	if (var == nullptr) {
		throw user_error(ref_expr->get_location(), "undefined symbol " c_id("%s"), token.text.c_str());
	}

	types::type_t::ref type = var->type->get_type()->eval(scope);

	/* we can't change the type of a mutable name in the scope because the user is allowed
	 * to assign a non-null value, and we don't want to take that away from them */
	if (!type->eval_predicate(tb_ref, nullptr)) {
		if (auto maybe_type = dyncast<const types::type_maybe_t>(type)) {
			runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
			assert(runnable_scope != nullptr);

			/* variable declarations begin new scopes */
			runnable_scope_t::ref fresh_scope = runnable_scope->new_runnable_scope(
					string_format("nullify-%s", token.text.c_str()));

			scope = fresh_scope;
			*new_scope = fresh_scope;

			scope->put_bound_variable(token.text,
					get_null(builder, scope, ref_expr->get_location()));
		} else {
			/* this is not a maybe, so let's just move along */
		}
	}
}

void extract_just_value(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		ast::reference_expr_t::ref ref_expr,
		bound_var_t::ref value,
		runnable_scope_t::ref *scope_if_true)
{
	auto casted_input = cast_data_type_to_ctor_struct(
			builder, scope, ref_expr->get_location(),
			value, token_t(INTERNAL_LOC(), tk_identifier, "Just"));

	bound_var_t::ref member = extract_member_by_index(
			builder,
			scope,
			life,
			ref_expr->get_location(),
			casted_input,
			casted_input->type,
			0,
			"value",
			false /*as_ref*/);

	runnable_scope_t::ref fresh_scope = scope->new_runnable_scope(
			string_format("just-%s", ref_expr->token.text.c_str()));

	*scope_if_true = fresh_scope;

	fresh_scope->put_bound_variable(ref_expr->token.text, member);
}

bound_var_t::ref resolve_null_check(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		location_t location,
		ast::item_t::ref node,
		bound_var_t::ref value,
		null_check_kind_t nck,
		runnable_scope_t::ref *scope_if_true,
		runnable_scope_t::ref *scope_if_false)
{
	if (!value->type->is_maybe(scope) && value->type->is_ptr(scope)) {
		auto error = user_error(location, "%s cannot be null here. "
				"if you must compare this to null, try casting it to a maybe pointer first.",
				node->str().c_str());
		error.add_info(location, "the type of %s is %s", node->str().c_str(), value->type->str().c_str());
		throw error;
	}

	bound_type_t::ref bound_bool_type = upsert_bound_type(builder, scope, type_id(make_iid(BOOL_TYPE)));
	assert(bound_bool_type != nullptr);
	llvm::Type *llvm_bool_type = bound_bool_type->get_llvm_specific_type();
	llvm::Value *llvm_bool_value;

	llvm::Value *llvm_value = value->resolve_bound_var_value(scope, builder);

	llvm::Constant *zero;
	if (llvm::dyn_cast<llvm::PointerType>(llvm_value->getType())) {
		zero = llvm::Constant::getNullValue(llvm_value->getType());
	} else if (llvm_value->getType()->isIntegerTy()) {
		zero = llvm::ConstantInt::get(llvm_value->getType(), 0);
	} else {
		assert(false);
	}

	switch (nck) {
	case nck_is_non_null:
		llvm_bool_value = builder.CreateIntCast(
				builder.CreateICmpNE(llvm_value, zero),
				llvm_bool_type, false /*isSigned*/);

		if (auto ref_expr = dyncast<const ast::reference_expr_t>(node)) {
			if (scope_if_true != nullptr) {
				unmaybe_variable(builder, scope, life, ref_expr, scope_if_true);
			}
			if (scope_if_false != nullptr) {
				nullify_let_var(builder, scope, life, ref_expr, scope_if_false);
			}
		}
		break;
	case nck_is_null:
		llvm_bool_value = builder.CreateIntCast(
				builder.CreateICmpEQ(llvm_value, zero),
				llvm_bool_type, false /*isSigned*/);

		if (auto ref_expr = dyncast<const ast::reference_expr_t>(node)) {
			if (scope_if_false != nullptr) {
				unmaybe_variable(builder, scope, life, ref_expr, scope_if_false);
			}
			if (scope_if_true != nullptr) {
				nullify_let_var(builder, scope, life, ref_expr, scope_if_true);
			}
		}
		break;
	}

	assert(llvm_bool_value != nullptr);
	return bound_var_t::create(
			INTERNAL_LOC(), "nullcheck",
			bound_bool_type, llvm_bool_value, make_iid("nullcheck"));
}
