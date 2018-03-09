#include "zion.h"
#include "atom.h"
#include "ast.h"
#include "code_id.h"
#include "llvm_utils.h"
#include "type_checker.h"
#include "compiler.h"
#include "llvm_types.h"
#include <iostream>
#include "unification.h"
#include "coercions.h"

void ast::when_block_t::resolve_statement(
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		life_t::ref life,
	   	local_scope_t::ref *,
	   	bool *returns) const
{
	assert(life->life_form == lf_statement);

	auto pattern_value = value->resolve_expression(builder,
			scope, life, true /*as_ref*/, nullptr);

	/* we don't care about references in pattern matching */
	pattern_value = pattern_value->resolve_bound_value(builder, scope);

	bool is_managed = false;
	pattern_value->type->is_managed_ptr(builder, scope, is_managed);

	if (!is_managed) {
		throw user_error(value->get_location(),
				"when statements only work with managed types. %s is a native type.",
				pattern_value->type->str().c_str());
	}
	if (pattern_value->type->is_maybe(scope)) {
		throw user_error(value->get_location(),
				"null pattern values are not allowed. "
				"check for null beforehand");
	}

	std::set<int> possible_incoming_typeids;
	types::get_runtime_typeids(pattern_value->type->get_type(), scope, possible_incoming_typeids);

	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);
	identifier::ref var_name;
	if (auto ref_expr = dyncast<const ast::reference_expr_t>(value)) {
		/* this is a single variable reference, which we can override in our pattern_blocks */
		// TODO: handle assignment (when x := f(a.b.c) ...) as a special form
		// to allow for naming more complex expressions
		var_name = make_code_id(ref_expr->token);
	} else {
		/* create a fake name for this pattern match expression */
		var_name = make_iid(types::gensym()->get_name());
	}

	bool all_patterns_return = true;
	bool else_returns = false;
	bound_var_t::ref type_id = call_typeid(scope, life, shared_from_this(),
			make_iid("value.to.match"), builder, pattern_value);

	llvm::Function *llvm_function_current = llvm_get_function(builder);
	llvm::BasicBlock *merge_block = nullptr;
	llvm::BasicBlock *default_block = llvm::BasicBlock::Create(builder.getContext(), "pattern.default", llvm_function_current);

	if (else_block != nullptr) {
		/* let's start by emitting the default block */
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		builder.SetInsertPoint(default_block);

		else_block->resolve_statement(builder, scope, life, nullptr, &else_returns);
		assert_implies(else_returns, builder.GetInsertBlock()->getTerminator() != nullptr);
		if (!else_returns && builder.GetInsertBlock()->getTerminator() == nullptr) {
			/* we must create a merge block because the default block doesn't return */
			if (merge_block == nullptr) {
				merge_block = llvm::BasicBlock::Create(builder.getContext(), "pattern.merge", llvm_function_current);
			}
			assert(builder.GetInsertBlock()->getTerminator() == nullptr);
			builder.CreateBr(merge_block);
		}
	} else {
		/* we still need a stub implementation in the default block to make LLVM happy that
		 * all paths return */
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		builder.SetInsertPoint(default_block);
		llvm_generate_dead_return(builder, scope);
	}

	std::map<int, location_t> typeids_tested;
	types::type_t::refs types_matched;
	llvm::SwitchInst *llvm_switch = builder.CreateSwitch(type_id->get_llvm_value(), default_block, pattern_blocks.size());
	/* RTTI lives on the nominal type plane */
	auto bindings = scope->get_type_variable_bindings();
	for (auto pattern_block : pattern_blocks) {
		auto type_to_match_raw = pattern_block->type->rebind(scope->get_type_variable_bindings())->eval(scope);
		types::type_t::refs types_to_match;
		auto pattern_type_to_match = promote_to_managed_type(
				type_to_match_raw,
				scope);

		if (auto type_sum = dyncast<const types::type_sum_t>(pattern_type_to_match)) {
			types_to_match = type_sum->options;
		} else {
			types_to_match.push_back(pattern_type_to_match);
		}

		std::set<int> typeids;
		types::type_t::refs reified_types;

		/* ok, now we should be matching within types_to_match */
		for (auto type_to_match : types_to_match) {

			/* types_matched means we've pretended that we have covered this inbound
			 * type */
			types_matched.push_back(type_to_match);

			if (type_to_match->ftv_count() != 0) {
				debug_above(6, log("skipping type %s within pattern case %s at %s because it has free type variables",
							type_to_match->str().c_str(),
							pattern_block->type->str().c_str(),
							pattern_block->get_location().str().c_str()));
				continue;
			}

			debug_above(6, log("attempting to match type %s", type_to_match->str().c_str()));

			if (!types::is_managed_ptr(type_to_match, scope)) {
				throw user_error(pattern_block->type->get_location(),
						"unable to find runtime type identity for %s. runtime type identity is needed in order to perform type matching",
						pattern_block->type->str().c_str());
			}

			int _typeid = atomize(type_to_match->repr());

			/* check that this type have not already been tested for */
			if (in(_typeid, typeids_tested)) {
				auto error = user_error(pattern_block->get_location(),
						"runtime type matching for %s is already handled above. note that it may be a part of this type, and not the whole sum of the type",
						type_to_match->str().c_str());
				error.add_info(typeids_tested[_typeid], "see prior test for runtime type here");
				throw error;
			}
			typeids.insert(_typeid);
			typeids_tested[_typeid] = pattern_block->get_location();

			reified_types.push_back(type_to_match);
		}

		if (reified_types.size() == 0) {
			/* there was really nothing to check from that type pattern because of
			 * reasons */
			continue;
		}

		/* create a new block for catching the pattern jump */
		llvm::BasicBlock *llvm_pattern_block = llvm::BasicBlock::Create(
				builder.getContext(),
				"pattern " + pattern_block->type->repr(),
				llvm_function_current);

		/* add the new blocks to the switch */
		for (auto _typeid : typeids) {
			llvm_switch->addCase(builder.getInt32(_typeid), llvm_pattern_block);
		}

		/* remember where we were */
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);

		/* start emitting code in the block */
		builder.SetInsertPoint(llvm_pattern_block);

		auto matched_type = type_sum_safe(reified_types, pattern_block->get_location(), scope);

		/* set up the variable to be interpreted as the type we've matched */
		scope_t::ref pattern_scope = runnable_scope->new_local_scope(string_format("pattern.%s", matched_type->str().c_str()));

		auto bound_matched_type = upsert_bound_type(builder, scope, matched_type);
		llvm::Value *llvm_pattern_value = coerce_value(
				builder,
				scope,
				life,
				pattern_value->get_location(),
				matched_type,
				pattern_value);

		/* replace this bound variable with a version of itself with a new type */
		pattern_scope->put_bound_variable(var_name->get_name(),
				bound_var_t::create(
					var_name->get_location(),
					var_name->get_name(),
					bound_matched_type,
					llvm_pattern_value,
					var_name));

		bool pattern_returns = false;
		pattern_block->block->resolve_statement(builder, pattern_scope, life, nullptr, &pattern_returns);

		assert_implies(pattern_returns, builder.GetInsertBlock()->getTerminator() != nullptr);
		if (!pattern_returns && builder.GetInsertBlock()->getTerminator() == nullptr) {
			/* if this block didn't return or break/continue, then we need to make sure we can merge
			 * to the next block */
			all_patterns_return = false;
			if (merge_block == nullptr) {
				merge_block = llvm::BasicBlock::Create(builder.getContext(), "pattern.merge", llvm_function_current);
			}
			assert(builder.GetInsertBlock()->getTerminator() == nullptr);
			builder.CreateBr(merge_block);
		}
	}

	if (merge_block != nullptr) {
		/* make sure that if we needed a merge block, that any downstream codegen knows
		 * to pick up from here when emitting code */
		builder.SetInsertPoint(merge_block);
	}

	/* check whether all cases of the pattern_value's type are handled */
	types::type_sum_t::ref type_sum_matched = type_sum_safe(types_matched, get_location(), scope);

	unification_t unification = unify(type_sum_matched, pattern_value->type->get_type(), scope);
	if (unification.result) {
		if (else_block == nullptr) {
			/* good, the user knew not to have an else block because they are handling
			 * all paths */
			*returns = all_patterns_return;
			return;
		} else {
			throw user_error(else_block->get_location(), "this else block will never run because the patterns catch all cases. maybe you can delete it?");
		}
	} else {
		/* the patterns don't cover all possible values */
		if (else_block == nullptr) {
			auto error = user_error(get_location(), "the 'when' block does not handle all inbound types %s",
					unification.str().c_str());
			error.add_info(get_location(), "the when block covers %s", type_sum_matched->str().c_str());
			throw error;
		} else {
			/* they didn't cover all the patterns, but they have an else block to
			 * catch what they missed. fine. */
			*returns = all_patterns_return && else_returns;
			return;
		}
	}
}

bound_var_t::ref gen_null_check(
		llvm::IRBuilder<> &builder,
		ast::item_t::ref node,
		scope_t::ref scope,
		life_t::ref life,
		identifier::ref value_name,
		bound_var_t::ref value,
		local_scope_t::ref *new_scope)
{
	value = value->resolve_bound_value(builder, scope);
	if (!value->type->is_ptr(scope)) {
		throw user_error(node->get_location(),
				"type %s cannot be compared to null", value->type->str().c_str());
	}

	value = value->resolve_bound_value(builder, scope);
	assert(llvm::dyn_cast<llvm::PointerType>(value->type->get_llvm_specific_type()));
	return value;
}

bound_var_t::ref gen_type_check(
		llvm::IRBuilder<> &builder,
		ast::item_t::ref node,
		scope_t::ref scope,
		life_t::ref life,
		identifier::ref value_name,
		bound_var_t::ref value,
		bound_type_t::ref bound_type,
		local_scope_t::ref *new_scope)
{
	if (bound_type->get_type()->eval_predicate(tb_null, scope)) {
		/* checking for the null type means checking for a zero value from a
		 * pointer. */
		return gen_null_check(builder, node, scope, life, value_name, value, new_scope);
	}

	value = value->resolve_bound_value(builder, scope);
	assert(life->life_form == lf_statement);

	auto program_scope = scope->get_program_scope();
	std::string signature = bound_type->get_type()->get_signature();
	auto type_id_wanted = bound_var_t::create(
			INTERNAL_LOC(),
			string_format("typeid(%s)", value_name->str().c_str()),
			program_scope->get_bound_type({TYPEID_TYPE}),
			llvm_create_int32(builder, atomize(signature)),
			value_name);

	debug_above(2, log(log_info, "generating a runtime type check "
				"of variable %s for type %s with signature value %d (for '%s') (type is %s)",
				value->str().c_str(),
				bound_type->str().c_str(), atomize(signature),
				signature.c_str(), bound_type->get_type()->str().c_str()));
	bound_var_t::ref type_id = call_typeid(scope, life, node,
			value_name, builder, value);

	auto get_typeid_eq_function = program_scope->get_bound_variable(
			node->get_location(), "__type_id_eq_type_id");

	assert(get_typeid_eq_function != nullptr);
	if (new_scope != nullptr) {
		if (auto runnable_scope = dyncast<runnable_scope_t>(scope)) {
			/* generate a new scope with the value_name containing a new
			 * variable to overwrite the prior scoped variable's type with
			 * the new checked type */
			*new_scope = runnable_scope->new_local_scope(string_format("when %s %s",
						value_name->str().c_str(),
						node->str().c_str()));

			/* replace this bound variable with a version of itself with a new type */
			(*new_scope)->put_bound_variable(value_name->get_name(),
					bound_var_t::create(
						value_name->get_location(),
						value_name->get_name(),
						bound_type,
						/* perform a safe runtime cast of this value */
						value->get_llvm_value(),
						value_name));
		}
	}

	/* call the type_id comparator function */
	return create_callsite(
			builder,
			scope,
			life,
			get_typeid_eq_function,
			value_name->get_name(),
			value_name->get_location(),
			{type_id, type_id_wanted});
}
