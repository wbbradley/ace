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
		bool *returns)
{
	llvm::Function *llvm_function_current = llvm_get_function(builder);
	llvm::BasicBlock *merge_block = nullptr;
	llvm::BasicBlock *default_block = llvm::BasicBlock::Create(builder.getContext(), "pattern.default", llvm_function_current);

	/* we may need a stub implementation in the default block to make LLVM happy that
	 * all paths return */
	{
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		builder.SetInsertPoint(default_block);
		llvm_generate_dead_return(builder, scope);
	}

	llvm::BasicBlock *llvm_next_merge = default_block;

	bool all_patterns_return = true;
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
		predicate->resolve_match(
				builder, scope, life,
				location,
				pattern_value,
				llvm_pattern_block,
				llvm_next_merge,
				&scope_if_match);

		/* save this as the "next" block to jump to (since we are traversing the patterns in
		 * reverse order */
		llvm_next_merge = check_block;

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

void check_patterns(
		runnable_scope_t::ref runnable_scope,
		location_t location,
		const ast::pattern_block_t::refs &pattern_blocks,
		bound_var_t::ref pattern_value)
{
	patterns::Pattern::ref uncovered = make_ptr<patterns::AllOf>(pattern_value->type->get_type());
	for (auto pattern_block : pattern_blocks) {
		patterns::Pattern::ref covering = pattern_block->predicate->get_pattern();
		if (patterns::pattern_intersect(uncovered, covering)->asNothing() != nullptr) {
			auto error = user_error(pattern_block->get_location(), "pattern will never match any possible values");
			error.add_info(pattern_block->get_location(), "uncovered values at this spot: %s",
					uncovered->str().c_str());
			throw error;
		}

		uncovered = patterns::pattern_difference(uncovered, covering);
	}

	if (uncovered->asNothing() == nullptr) {
			auto error = user_error(location, "not all patterns are covered");
			error.add_info(location, "uncovered patterns: %s",
					uncovered->str().c_str());
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

	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);

	check_patterns(runnable_scope, value->get_location(), pattern_blocks, pattern_value);
	build_patterns(
			builder,
			runnable_scope,
			life,
			value->get_location(),
			builder.GetInsertBlock(),
			pattern_blocks,
			pattern_value,
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

void ast::literal_expr_t::resolve_match(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		location_t value_location,
		bound_var_t::ref input_value,
		llvm::BasicBlock *llvm_match_block,
		llvm::BasicBlock *llvm_no_match_block,
		runnable_scope_t::ref *scope_if_true) const
{
	assert(false);
}

bound_var_t::ref cast_data_type_to_ctor_struct(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		location_t value_location,
		bound_var_t::ref input_value,
		token_t ctor_name)
{
	types::type_data_t::ref data_type = dyncast<const types::type_data_t>(
			input_value->type->get_type()->eval(scope));
	if (data_type == nullptr) {
		throw user_error(input_value->get_location(), "unable to find data type in %s", input_value->str().c_str());
	}

	for (auto ctor_pair : data_type->ctor_pairs) {
		if (ctor_pair.first.text == ctor_name.text) {
			auto bound_type = upsert_bound_type(builder, scope, type_ptr(type_managed(type_struct(ctor_pair.second))));
			return bound_var_t::create(
					INTERNAL_LOC(),
					ctor_name.text,
					bound_type,
					llvm_maybe_pointer_cast(builder,
						input_value->get_llvm_value(), bound_type->get_llvm_specific_type()),
					make_code_id(ctor_pair.first));
		}
	}
	throw user_error(ctor_name.location, "unable to find value of " c_id("%s") " in %s",
			ctor_name.text.c_str(),
			input_value->str().c_str());
	return nullptr;
}

void ast::ctor_predicate_t::resolve_match(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		location_t value_location,
		bound_var_t::ref input_value,
		llvm::BasicBlock *llvm_match_block,
		llvm::BasicBlock *llvm_no_match_block,
		runnable_scope_t::ref *scope_if_true) const
{
	llvm::Function *llvm_function_current = llvm_get_function(builder);
	bound_var_t::ref input_ctor_id = call_get_ctor_id(scope, life, shared_from_this(),
			make_iid("input_ctor_id"), builder, input_value);

	int ctor_id = atomize(token.text);
	debug_above(7, log("matching ctor id %s = %d", token.text.c_str(), ctor_id));

	/* check that this is the right ctor */
	llvm::Value *match_bit = builder.CreateICmpEQ(
			input_ctor_id->get_llvm_value(),
		   	builder.getInt32(ctor_id));
	match_bit->setName("ctor." + token.text + ".matched");

	auto casted_input = cast_data_type_to_ctor_struct(
			builder, scope, value_location, input_value, token);
	
	llvm::BasicBlock *llvm_next_check = llvm_match_block;
	runnable_scope_t::ref scope_if_match_at_end = scope;

	for (int i = params.size()-1; i >= 0; --i) {
		llvm::BasicBlock *check_block = llvm::BasicBlock::Create(
				builder.getContext(),
				"check." + params[i]->repr(),
				llvm_function_current);
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		builder.SetInsertPoint(check_block);
		bound_var_t::ref member = extract_member_by_index(
				builder,
				scope,
				life,
				params[i]->get_location(),
				casted_input,
				casted_input->type,
				i,
				params[i]->token.text,
				false /*as_ref*/);

		/* resolve sub-patterns */
		runnable_scope_t::ref scope_if_match = nullptr;
		params[i]->resolve_match(builder, scope_if_match_at_end, life, 
				value_location, member, llvm_next_check, llvm_no_match_block, &scope_if_match);
		if (scope_if_match != nullptr) {
			scope_if_match_at_end = scope_if_match;
		}
		llvm_next_check = check_block;
	}

	/* by this point llvm_next_check should point to either the next thing we need to check or the
	 * final pattern block */
	builder.CreateCondBr(match_bit, llvm_next_check, llvm_no_match_block);
	*scope_if_true = scope_if_match_at_end;
}

void ast::irrefutable_predicate_t::resolve_match(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		location_t value_location,
		bound_var_t::ref input_value,
		llvm::BasicBlock *llvm_match_block,
		llvm::BasicBlock *,
		runnable_scope_t::ref *scope_if_true) const
{
	if (!(token.is_ident(K(_)) || token.is_ident(K(else)))) {
		*scope_if_true = scope->new_runnable_scope("irrefutable." + token.text);
		(*scope_if_true)->put_bound_variable(token.text, input_value);
	}

	/* throw away this value, and continue */
	builder.CreateBr(llvm_match_block);
}
