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

void build_patterns(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		location_t location,
		llvm::BasicBlock *llvm_start_block,
		const ast::pattern_block_t::refs &pattern_blocks,
		bound_var_t::ref pattern_value,
		bound_var_t::ref rtti_encoding,
		identifier::ref var_name,
		ast::block_t::ref else_block,
		bool *returns)
{
	llvm::Function *llvm_function_current = llvm_get_function(builder);
	llvm::BasicBlock *merge_block = nullptr;
	llvm::BasicBlock *default_block = llvm::BasicBlock::Create(builder.getContext(), "pattern.default", llvm_function_current);

	bool else_returns = false;
	if (else_block != nullptr) {
		/* let's start by emitting the default block */
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		builder.SetInsertPoint(default_block);

		else_block->resolve_statement(builder, scope, life, nullptr, &else_returns);
		assert_implies(else_returns, builder.GetInsertBlock()->getTerminator() != nullptr);
		if (!else_returns && builder.GetInsertBlock()->getTerminator() == nullptr) {
			/* we must create a merge block because the default block doesn't return */
			merge_block = llvm::BasicBlock::Create(builder.getContext(), "pattern.merge", llvm_function_current);
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

	llvm::BasicBlock *llvm_next_merge = default_block;

	bool all_patterns_return = true;
	/* RTTI lives on the nominal type plane */
	auto bindings = scope->get_type_variable_bindings();
	for (auto pattern_block_iter = pattern_blocks.rbegin(); pattern_block_iter != pattern_blocks.rend(); ++pattern_block_iter) {
		auto pattern_block = *pattern_block_iter;
		auto type_to_match_raw = pattern_block->type->rebind(scope->get_type_variable_bindings())->eval(scope);

		types::type_t::refs types_to_match;
		auto pattern_type_to_match = promote_to_managed_type(
				type_to_match_raw,
				scope);

		llvm::BasicBlock *check_block = llvm::BasicBlock::Create(builder.getContext(), "test." + pattern_type_to_match->repr(), llvm_function_current);
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		builder.SetInsertPoint(check_block);

		/* create a new block for catching the pattern jump */
		llvm::BasicBlock *llvm_pattern_block = llvm::BasicBlock::Create(
				builder.getContext(),
				"matched." + pattern_block->type->repr(),
				llvm_function_current);

		auto matcher = scope->get_program_scope()->make_matcher(builder,
				pattern_block->type->get_location(), pattern_type_to_match);

		std::vector<llvm::Value *> matching_params;
		matching_params.push_back(rtti_encoding->get_llvm_value());
		matching_params.push_back(builder.getInt32(0));
		llvm::CallInst *llvm_type_match = llvm_create_call_inst(
				builder,
				pattern_block->get_location(),
				matcher,
				matching_params);

		/* branch upon testing the type */
		llvm::Constant *zero = llvm::ConstantInt::get(llvm_type_match->getType(), 0);
		builder.CreateCondBr(
				builder.CreateICmpNE(llvm_type_match, zero),
				llvm_pattern_block, llvm_next_merge);

		/* save this as the "next" block to jump to (since we are travesering the patterns in
		 * reverse order */
		llvm_next_merge = check_block;

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
			assert(false);
			continue;
		}

		/* remember where we were */
		llvm::IRBuilderBase::InsertPointGuard ipg2(builder);

		/* start emitting code in the block */
		builder.SetInsertPoint(llvm_pattern_block);

		auto matched_type = type_sum_safe(reified_types, pattern_block->get_location(), scope);

		/* set up the variable to be interpreted as the type we've matched */
		scope_t::ref pattern_scope = scope->new_runnable_scope(string_format("pattern.%s", matched_type->str().c_str()));

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

	{
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		builder.SetInsertPoint(llvm_start_block);
		builder.CreateBr(llvm_next_merge);
	}

	if (merge_block != nullptr) {
		/* make sure that if we needed a merge block, that any downstream codegen knows
		 * to pick up from here when emitting code */
		builder.SetInsertPoint(merge_block);
	}

	/* check whether all cases of the pattern_value's type are handled */
	types::type_sum_t::ref type_sum_matched = type_sum_safe(types_matched, location, scope);

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
			auto error = user_error(location, "the 'when' block does not handle all inbound types %s",
					unification.str().c_str());
			error.add_info(location, "the when block covers %s", type_sum_matched->str().c_str());
			throw error;
		} else {
			/* they didn't cover all the patterns, but they have an else block to
			 * catch what they missed. fine. */
			*returns = all_patterns_return && else_returns;
			return;
		}
	}
}

void ast::when_block_t::resolve_statement(
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		life_t::ref life,
	   	runnable_scope_t::ref *,
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
		auto error = user_error(value->get_location(),
				"null pattern values are not allowed. "
				"check for null beforehand");
		error.add_info(pattern_value->get_location(), "pattern value has type %s", pattern_value->type->str().c_str());
		throw error;
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

	bound_var_t::ref rtti_encoding = call_get_var_rtti(scope, life, shared_from_this(),
			make_iid("value.to.match"), builder, pattern_value);

	build_patterns(
			builder,
			runnable_scope,
			life,
			get_location(),
			builder.GetInsertBlock(),
			pattern_blocks,
			pattern_value,
			rtti_encoding,
			var_name,
			else_block,
			returns);
}

bound_var_t::ref gen_null_check(
		llvm::IRBuilder<> &builder,
		ast::item_t::ref node,
		scope_t::ref scope,
		life_t::ref life,
		identifier::ref value_name,
		bound_var_t::ref value,
		runnable_scope_t::ref *new_scope)
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
