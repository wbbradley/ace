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
		bound_var_t::ref input_ctor_id,
		bool *returns)
{
	llvm::Function *llvm_function_current = llvm_get_function(builder);
	llvm::BasicBlock *merge_block = nullptr;
	llvm::BasicBlock *default_block = llvm::BasicBlock::Create(builder.getContext(), "pattern.default", llvm_function_current);

	/* we may need a stub implementation in the default block to make LLVM happy that
	 * all paths return */
	llvm::IRBuilderBase::InsertPointGuard ipg(builder);
	builder.SetInsertPoint(default_block);
	llvm_generate_dead_return(builder, scope);

	std::map<int, location_t> typeids_tested;
	types::type_t::refs types_matched;

	llvm::BasicBlock *llvm_next_merge = default_block;

	bool all_patterns_return = true;
	auto bindings = scope->get_type_variable_bindings();
	for (auto pattern_block_iter = pattern_blocks.rbegin(); pattern_block_iter != pattern_blocks.rend(); ++pattern_block_iter) {
		auto pattern_block = *pattern_block_iter;
		auto predicate = pattern_block->predicate;

		std::string pattern_name = predicate->repr();
		llvm::BasicBlock *check_block = llvm::BasicBlock::Create(
				builder.getContext(),
			   	"test." + pattern_name,
			   	llvm_function_current);

		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		builder.SetInsertPoint(check_block);

		/* create a new block for catching the pattern jump */
		llvm::BasicBlock *llvm_pattern_block = llvm::BasicBlock::Create(
				builder.getContext(),
				"matched." + pattern_name,
				llvm_function_current);

		runnable_scope_t::ref scope_if_match;
		bound_var_t::ref matched = predicate->resolve_match(
				builder, scope, life,
				input_ctor_id,
				&scope_if_match);

		/* branch upon testing the type - zero means no-match */
		llvm::Constant *zero = llvm::ConstantInt::get(matched->get_llvm_value()->getType(), 0);
		builder.CreateCondBr(
				builder.CreateICmpNE(matched->get_llvm_value(), zero),
				llvm_pattern_block, llvm_next_merge);

		/* save this as the "next" block to jump to (since we are travesering the patterns in
		 * reverse order */
		llvm_next_merge = check_block;

		/* remember where we were */
		llvm::IRBuilderBase::InsertPointGuard ipg2(builder);

		/* start emitting code in the block */
		builder.SetInsertPoint(llvm_pattern_block);

		/* set up the variable to be interpreted as the type we've matched */
		scope_t::ref pattern_scope = (
				(scope_if_match != nullptr)
				? scope_if_match
				: scope->new_runnable_scope(string_format("pattern.%s", pattern_name.c_str())));

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
	bool all_values_matched = true; // TODO: <--- figure out if this is true or not
	if (all_values_matched) {
		/* good, the user knew not to have an else block because they are handling
		 * all paths */
		*returns = all_patterns_return;
		return;
	} else {
		/* the patterns don't cover all possible values */
		auto error = user_error(location, "the 'when' block does not handle all inbound values");
		throw error;
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

	bound_var_t::ref input_ctor_id = call_get_ctor_id(scope, life, shared_from_this(),
			make_iid("input_ctor_id"), builder, pattern_value);

	build_patterns(
			builder,
			runnable_scope,
			life,
			get_location(),
			builder.GetInsertBlock(),
			pattern_blocks,
			pattern_value,
			input_ctor_id,
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

bound_var_t::ref ast::literal_expr_t::resolve_match(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bound_var_t::ref input_value,
		runnable_scope_t::ref *scope_if_true) const
{
	assert(false);
	return nullptr;
}

bound_var_t::ref ast::ctor_predicate_t::resolve_match(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bound_var_t::ref input_value,
		runnable_scope_t::ref *scope_if_true) const
{
	assert(false);
	return nullptr;
}

bound_var_t::ref ast::irrefutable_predicate_t::resolve_match(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bound_var_t::ref input_value,
		runnable_scope_t::ref *scope_if_true) const
{
	assert(false);
	return nullptr;
}
