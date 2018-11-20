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

types::type_t::ref build_patterns(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		location_t location,
		llvm::BasicBlock *llvm_start_block,
		llvm::BasicBlock *merge_block,
		const ast::pattern_block_t::refs &pattern_blocks,
		bound_var_t::ref pattern_value,
		bool *returns,
		types::type_t::ref expected_type,
		std::list<std::pair<bound_var_t::ref, llvm::BasicBlock *>> &incoming_values)
{
	llvm::Function *llvm_function_current = llvm_get_function(builder);
	llvm::BasicBlock *default_block = llvm::BasicBlock::Create(builder.getContext(), "pattern.default", llvm_function_current);

	/* we may need a stub implementation in the default block to make LLVM happy that
	 * all paths return */
	{
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		builder.SetInsertPoint(default_block);
		runnable_scope_t::ref new_scope;
		resolve_assert_macro(builder, scope, life,
			   	token_t{location, tk_string, "assert"},
				ast::create<ast::reference_expr_t>(token_t{location, tk_identifier, "false"}), &new_scope);
		llvm_generate_dead_return(builder, scope);
	}

	llvm::BasicBlock *llvm_next_merge = default_block;
	assert(pattern_blocks.size() != 0);

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
		if (!predicate->resolve_match(
					builder, scope, life,
					location,
					pattern_value,
					llvm_pattern_block,
					llvm_next_merge,
					&scope_if_match))
		{
			/* this pattern cannot match because the incoming type is unbound */
			assert(!builder.GetInsertBlock()->getTerminator());
			builder.CreateBr(llvm_next_merge);

			assert(llvm_pattern_block->getTerminator() == nullptr);
			llvm::IRBuilderBase::InsertPointGuard ipg(builder);
			builder.SetInsertPoint(llvm_pattern_block);
			assert(!builder.GetInsertBlock()->getTerminator());
			builder.CreateBr(llvm_next_merge);

			llvm_next_merge = check_block;
		} else {

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
			bound_var_t::ref block_value;
			if (expected_type != type_bottom()) {
				block_value = pattern_block->block->resolve_expression(
						builder, pattern_scope, life, false /*as_ref*/, expected_type, &pattern_returns);
				if (block_value == nullptr) {
					/* block_value probably returned, so it has no value... */
					assert(pattern_returns);
				} else if (block_value->type->is_bottom(scope)) {
					builder.CreateUnreachable();
					pattern_returns = true;
				} else {
					/* we are in an expression */
					unification_t unification = unify(expected_type, block_value->type->get_type(), pattern_scope);
					if (!unification.result) {
						auto error = user_error(block_value->get_location(), "value does not have a cohesive type with the rest of the match expression");
						error.add_info(expected_type == type_unit() ? location : expected_type->get_location(), "expected type %s", expected_type->str().c_str());
						throw error;
					} else {
						/* update expected type to ensure we are narrowing what is acceptable */
						expected_type = expected_type->rebind(unification.bindings);
						assert(expected_type != type_bottom());
					}
				}
			} else {
				pattern_block->block->resolve_statement(builder, pattern_scope, life, nullptr, &pattern_returns);
			}

			if (!pattern_returns && builder.GetInsertBlock()->getTerminator() == nullptr) {
				/* if this block didn't return or break/continue, then we need to make sure we can merge
				 * to the next block */
				all_patterns_return = false;
				assert(builder.GetInsertBlock()->getTerminator() == nullptr);
				if (expected_type != type_bottom()) {
					assert(!block_value->type->is_bottom(scope));
					incoming_values.push_back(std::pair<bound_var_t::ref, llvm::BasicBlock*>{block_value, builder.GetInsertBlock()});
				}
				assert(!builder.GetInsertBlock()->getTerminator());
				builder.CreateBr(merge_block);
			}
		}
	}

	{
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		builder.SetInsertPoint(llvm_start_block);
		assert(!builder.GetInsertBlock()->getTerminator());
		builder.CreateBr(llvm_next_merge);
	}

	if (merge_block != nullptr) {
		/* make sure that if we needed a merge block, that any downstream codegen knows
		 * to pick up from here when emitting code */
		builder.SetInsertPoint(merge_block);
	}

	/* good, the user knew not to have an else block because they are handling
	 * all paths */
	*returns = all_patterns_return;
    if (all_patterns_return && expected_type != type_bottom()) {
        throw user_error(
                expected_type->get_location(),
                "you will never get a value here");
    }

	return expected_type;
}

void check_patterns(
		runnable_scope_t::ref runnable_scope,
		location_t location,
		std::string expr,
		const ast::pattern_block_t::refs &pattern_blocks,
		bound_var_t::ref pattern_value)
{
	match::Pattern::ref uncovered = match::all_of(location, expr, runnable_scope, pattern_value->type->get_type());
	for (auto pattern_block : pattern_blocks) {
		match::Pattern::ref covering = pattern_block->predicate->get_pattern(pattern_value->type->get_type(), runnable_scope);
		if (match::intersect(uncovered, covering)->asNothing() != nullptr) {
			auto error = user_error(pattern_block->get_location(), "this pattern is already covered");
			if (uncovered->asNothing() != nullptr) {
				error.add_info(pattern_block->get_location(), "there is nothing left to match by this point");
			} else {
				error.add_info(pattern_block->get_location(), "so far you haven't covered: %s",
						uncovered->str().c_str());
			}
			throw error;
		}

		debug_above(9, log("uncovered = %s", uncovered->str().c_str()));
		debug_above(9, log("covering = %s", covering->str().c_str()));
		uncovered = match::difference(uncovered, covering);
	}

	if (uncovered->asNothing() == nullptr) {
			auto error = user_error(location, "not all patterns are covered");
			error.add_info(location, "uncovered patterns: %s",
					uncovered->str().c_str());
			throw error;
	}
}

void ast::match_expr_t::resolve_statement(
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref scope,
		life_t::ref life,
	   	runnable_scope_t::ref *,
	   	bool *returns) const
{
	resolve_match_expr(builder, scope, life, false /*as_ref*/, returns, type_bottom());
}

bound_var_t::ref ast::match_expr_t::resolve_expression(
	llvm::IRBuilder<> &builder,
	scope_t::ref scope,
	life_t::ref life,
	bool as_ref,
	types::type_t::ref expected_type,
	bool *returns) const
{
	return resolve_match_expr(builder, scope, life, as_ref, returns,
		   	expected_type != nullptr ? expected_type : type_variable(token.location));
}


types::type_t::ref ast::match_expr_t::resolve_type(scope_t::ref scope, types::type_t::ref expected_type) const {
	assert(false);
	return nullptr;
}

bound_var_t::ref ast::match_expr_t::resolve_match_expr(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_t::ref life,
		bool as_ref,
		bool *returns,
		types::type_t::ref expected_type) const
{
	assert(expected_type != nullptr);
	assert(life->life_form == lf_statement);
	assert(returns != nullptr);

	auto pattern_value = value->resolve_expression(builder,
			scope, life, true /*as_ref*/, nullptr, returns);

	if (returns != nullptr && *returns) {
		throw user_error(value->get_location(), "this value will return so the match seems pointless?");
	}

	/* we don't care about references in pattern matching */
	pattern_value = pattern_value->resolve_bound_value(builder, scope);

	if (pattern_value->type->is_maybe(scope)) {
		auto error = user_error(value->get_location(),
				"null pattern values are not allowed. "
				"check for null beforehand");
		error.add_info(pattern_value->get_location(), "pattern value has type %s", pattern_value->type->str().c_str());
		throw error;
	}

	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(scope);

	check_patterns(
			runnable_scope,
			value->get_location(),
			value->str(),
			pattern_blocks,
			pattern_value);

	llvm::Function *llvm_function_current = llvm_get_function(builder);
	llvm::BasicBlock *merge_block = llvm::BasicBlock::Create(builder.getContext(), "pattern.merge", llvm_function_current);

	std::list<std::pair<bound_var_t::ref, llvm::BasicBlock *>> incoming_values;
	types::type_t::ref final_type = build_patterns(
			builder,
			runnable_scope,
			life,
			value->get_location(),
			builder.GetInsertBlock(),
			merge_block,
			pattern_blocks,
			pattern_value,
			returns,
			expected_type,
			incoming_values);

	if (*returns) {
		assert(final_type == type_bottom());
		merge_block->removeFromParent();
        return scope->get_program_scope()->get_singleton("__unit__");
	} else {
		debug_above(5, log_location(log_info, token.location, "checking match"));
		if (final_type != type_bottom()) {
			auto bound_final_type = upsert_bound_type(builder, runnable_scope, final_type);
			builder.SetInsertPoint(merge_block);
			llvm::PHINode *llvm_phi_node = llvm::PHINode::Create(
					bound_final_type->get_llvm_specific_type(),
					incoming_values.size(), "match.phi.node", merge_block);

			for (auto incoming_value : incoming_values) {
				llvm::IRBuilder<> builder(incoming_value.second);
				llvm_phi_node->addIncoming(
						coerce_value(
							builder, scope, life,
							incoming_value.first->get_location(),
							final_type,
							incoming_value.first),
						builder.GetInsertBlock());
			}
			return bound_var_t::create(
					INTERNAL_LOC(),
					"match.value",
					bound_final_type,
					llvm_phi_node,
					make_iid_impl("match.value", get_location()));
		} else {
			return scope->get_program_scope()->get_singleton("__unit__");
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

bool ast::literal_expr_t::resolve_match(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		location_t value_location,
		bound_var_t::ref input_value,
		llvm::BasicBlock *llvm_match_block,
		llvm::BasicBlock *llvm_no_match_block,
		runnable_scope_t::ref *scope_if_true) const
{
	if (input_value->type->get_type()->eval_predicate(tb_int, scope)) {
		llvm::Value *llvm_value_to_check = input_value->get_llvm_value();
		llvm::IntegerType *llvm_int_type = llvm::dyn_cast<llvm::IntegerType>(llvm_value_to_check->getType());
		if (llvm_int_type == nullptr) {
			throw user_error(token.location, "could not figure out how to compare %s to a %s",
					token.str().c_str(),
					input_value->type->get_type()->str().c_str());
		}
		auto bit_width = llvm_int_type->getBitWidth();
		llvm::Value *match_bit = builder.CreateICmpEQ(
				input_value->get_llvm_value(),
				builder.getIntN(bit_width, parse_int_value(token)));
		match_bit->setName("int_literal." + token.text + ".matched");
		builder.CreateCondBr(match_bit, llvm_match_block, llvm_no_match_block);
		return true;
	} else if (input_value->type->get_type()->eval_predicate(tb_str, scope)) {
		auto bound_bool_type = scope->get_program_scope()->get_bound_type(BOOL_TYPE);
		bound_var_t::ref string_to_test = create_global_str(builder, scope, token.location, unescape_json_quotes(token.text));
		bound_var_t::ref matched = call_program_function(
				builder,
				scope,
				life,
				"__eq__",
				token.location,
				{string_to_test, input_value},
				bound_bool_type->get_type());
		llvm::Value *match_bit = llvm_zion_bool_to_i1(builder, matched->get_llvm_value());
		match_bit->setName("str_literal." + token.text + ".matched");
		builder.CreateCondBr(match_bit, llvm_match_block, llvm_no_match_block);
		return true;
	}
	assert(false);
	return false;
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
			if (ctor_pair.second->str().find(BOTTOM_TYPE) != std::string::npos) {
				throw unbound_type_error(value_location, "ctor_pair has bottomed out");
			}
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

bool ast::ctor_predicate_t::resolve_match(
		llvm::IRBuilder<> &builder,
		runnable_scope_t::ref scope,
		life_t::ref life,
		location_t value_location,
		bound_var_t::ref input_value,
		llvm::BasicBlock *llvm_match_block,
		llvm::BasicBlock *llvm_no_match_block,
		runnable_scope_t::ref *scope_if_true) const
{
	bound_var_t::ref casted_input;
	try {
		casted_input = cast_data_type_to_ctor_struct(
				builder, scope, value_location, input_value, token);
	} catch (unbound_type_error &error) {
		/* this match is impossible because the type it is matching cannot be instantiated */
		return false;
	}

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

	llvm::BasicBlock *llvm_next_check = llvm_match_block;
	runnable_scope_t::ref scope_if_match_at_end = scope;

	for (int i = params.size()-1; i >= 0; --i) {
		llvm::BasicBlock *check_block = llvm::BasicBlock::Create(
				builder.getContext(),
				"check." + params[i]->repr(),
				llvm_function_current);
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		builder.SetInsertPoint(check_block);
		// TODO: allow as_ref below to be true
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
		if (!params[i]->resolve_match(builder, scope_if_match_at_end, life, 
				value_location, member, llvm_next_check, llvm_no_match_block, &scope_if_match))
		{
			assert(!builder.GetInsertBlock()->getTerminator());
			builder.CreateBr(llvm_no_match_block);
			return false;
		}

		if (scope_if_match != nullptr) {
			scope_if_match_at_end = scope_if_match;
		}
		llvm_next_check = check_block;
	}

	/* by this point llvm_next_check should point to either the next thing we need to check or the
	 * final pattern block */
	builder.CreateCondBr(match_bit, llvm_next_check, llvm_no_match_block);
	*scope_if_true = scope_if_match_at_end;

	if (name_assignment.tk == tk_identifier) {
		/* put a name on the pattern match. for example: a match on x@T(_, _) gets named x */
		*scope_if_true = (*scope_if_true)->new_runnable_scope("name_assignment." + name_assignment.text);
		(*scope_if_true)->put_bound_variable(name_assignment.text, input_value);
	}

	return true;
}

bool ast::tuple_predicate_t::resolve_match(
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
				input_value,
				input_value->type,
				i,
				params[i]->token.text,
				false /*as_ref*/);

		/* resolve sub-patterns */
		runnable_scope_t::ref scope_if_match = nullptr;
		if (!params[i]->resolve_match(builder, scope_if_match_at_end, life,
					value_location, member, llvm_next_check, llvm_no_match_block, &scope_if_match))
		{
			assert(!builder.GetInsertBlock()->getTerminator());
			builder.CreateBr(llvm_no_match_block);
			return false;
		}

		if (scope_if_match != nullptr) {
			scope_if_match_at_end = scope_if_match;
		}
		llvm_next_check = check_block;
	}

	/* by this point llvm_next_check should point to either the next thing we need to check or the
	 * final pattern block */
	builder.CreateBr(llvm_next_check);
	*scope_if_true = scope_if_match_at_end;

	if (name_assignment.tk == tk_identifier) {
		/* put a name on the pattern match. for example: a match on x@T(_, _) gets named x */
		*scope_if_true = (*scope_if_true)->new_runnable_scope("name_assignment." + name_assignment.text);
		(*scope_if_true)->put_bound_variable(name_assignment.text, input_value);
	}

	return true;
}

bool ast::irrefutable_predicate_t::resolve_match(
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
	assert(!builder.GetInsertBlock()->getTerminator());
	builder.CreateBr(llvm_match_block);
	return true;
}
