#include "zion.h"
#include "patterns.h"
#include "ast.h"
#include "compiler.h"
#include <iostream>
#include "translate.h"

using namespace bitter;

#if 0
types::type_t::ref build_patterns(
		location_t location,
		const ast::pattern_blocks_t &pattern_blocks,
		types::type_t::ref pattern_value_type,
		bool *returns,
		types::type_t::ref expected_type)
{
	assert(pattern_blocks.size() != 0);

	bool all_patterns_return = true;
	for (auto pattern_block_iter = pattern_blocks.rbegin(); pattern_block_iter != pattern_blocks.rend(); ++pattern_block_iter) {
		auto pattern_block = *pattern_block_iter;
		auto predicate = pattern_block->predicate;

		std::string pattern_name = predicate->repr();

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
				block_value = safe_dyncast<const bound_var_t>(
						pattern_block->block->resolve_expression(
							delegate, pattern_scope, life, false /*as_ref*/, expected_type,
							&pattern_returns));
				if (block_value == nullptr) {
					/* block_value probably returned, so it has no value... */
					assert(pattern_returns);
				} else if (block_value->get_type()->is_bottom(scope)) {
					builder.CreateUnreachable();
					pattern_returns = true;
				} else {
					auto unbottomed_expected_type = expected_type->rebind(pattern_scope->get_type_variable_bindings())->unbottom();

					/* we are in an expression */
					unification_t unification = unify(
							unbottomed_expected_type,
							block_value->get_type()->rebind(pattern_scope->get_type_variable_bindings())->unbottom(),
							pattern_scope);

					if (!unification.result) {
						auto error = user_error(block_value->get_location(), "value does not have a cohesive type with the rest of the match expression");
						error.add_info(expected_type == type_unit() ? location : expected_type->get_location(), "expected type %s", expected_type->str().c_str());
						throw error;
					} else {
						/* update expected type to ensure we are narrowing what is acceptable */
						expected_type = unbottomed_expected_type->rebind(unification.bindings);
						debug_above(6, log("refined expected type to %s", expected_type->str().c_str()));
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
					assert(!block_value->get_type()->is_bottom(scope));
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
#endif

void check_patterns(
		location_t location,
		std::string expr,
		const translation_env_t &tenv,
		const pattern_blocks_t &pattern_blocks,
		types::type_t::ref pattern_value_type)
{
	match::Pattern::ref uncovered = match::all_of(location, maybe<identifier_t>(make_iid(expr)), tenv, pattern_value_type);
	for (auto pattern_block : pattern_blocks) {
		match::Pattern::ref covering = pattern_block->predicate->get_pattern(
				pattern_value_type, tenv);
		if (match::intersect(uncovered, covering)->asNothing() != nullptr) {
			auto error = user_error(pattern_block->predicate->get_location(), "this pattern is already covered");
			if (uncovered->asNothing() != nullptr) {
				error.add_info(pattern_block->predicate->get_location(), "there is nothing left to match by this point");
			} else {
				error.add_info(pattern_block->predicate->get_location(), "so far you haven't covered: %s",
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

expr_t *translate_match_expr(
		const defn_id_t &for_defn_id,
		bitter::match_t *match,
		const std::unordered_set<std::string> &bound_vars,
		const translation_env_t &tenv,
		std::unordered_map<bitter::expr_t *, types::type_t::ref> &typing,
		std::set<defn_id_t> &needed_defns)
{
	bool returns_ = false;
	bool *returns = &returns_;
	auto expected_type = tenv.get_type(match);

	log("match expression is expecting type %s", expected_type->str().c_str());

	auto pattern_value = texpr(for_defn_id, match->scrutinee, bound_vars, tenv, typing, needed_defns);

	if (returns != nullptr && *returns) {
		throw user_error(pattern_value->get_location(), "this value will return so the match seems pointless?");
	}

	auto scrutinee_type = tenv.get_type(match->scrutinee);

	check_patterns(
			pattern_value->get_location(),
			match->scrutinee->str(),
			tenv,
			match->pattern_blocks,
			scrutinee_type);

#if 0
	build_patterns(
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
#endif
	// assert(false);
	return match;
}

#if 0
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
	if (input_value->get_type()->eval_predicate(tb_int, scope)) {
		llvm::Value *llvm_value_to_check = input_value->get_llvm_value(scope);
		llvm::IntegerType *llvm_int_type = llvm::dyn_cast<llvm::IntegerType>(llvm_value_to_check->getType());
		if (llvm_int_type == nullptr) {
			throw user_error(token.location, "could not figure out how to compare %s to a %s",
					token.str().c_str(),
					input_value->get_type()->str().c_str());
		}
		auto bit_width = llvm_int_type->getBitWidth();
		llvm::Value *match_bit = builder.CreateICmpEQ(
				input_value->get_llvm_value(scope),
				builder.getIntN(bit_width, parse_int_value(token)));
		match_bit->setName("int_literal." + token.text + ".matched");
		builder.CreateCondBr(match_bit, llvm_match_block, llvm_no_match_block);
		return true;
	} else if (input_value->get_type()->eval_predicate(tb_str, scope)) {
		auto bound_bool_type = scope->get_program_scope()->get_bound_type(BOOL_TYPE);
		bound_var_t::ref string_to_test = create_global_str(builder, scope, token.location, unescape_json_quotes(token.text));

		delegate_t delegate{builder, true};
		bound_var_t::ref matched = safe_dyncast<const bound_var_t>(
				call_program_function(
					delegate,
					scope,
					life,
					"__eq__",
					token.location,
					{string_to_test, input_value},
					bound_bool_type->get_type()));
		llvm::Value *match_bit = llvm_zion_bool_to_i1(builder, matched->get_llvm_value(scope));
		match_bit->setName("str_literal." + token.text + ".matched");
		builder.CreateCondBr(match_bit, llvm_match_block, llvm_no_match_block);
		return true;
	}
	assert(false);
	return false;
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
	delegate_t delegate{builder, true};
	bound_var_t::ref casted_input;
	try {
		casted_input = cast_data_type_to_ctor_struct(
				builder, scope, value_location, input_value, token);
	} catch (unbound_type_error &error) {
		/* this match is impossible because the type it is matching cannot be instantiated */
		return false;
	}

	llvm::Function *llvm_function_current = llvm_get_function(builder);
	bound_var_t::ref input_ctor_id = safe_dyncast<const bound_var_t>(
			call_get_ctor_id(builder, scope, life, shared_from_this(),
				make_iid("input_ctor_id"), input_value));

	int ctor_id = atomize(token.text);
	debug_above(7, log("matching ctor id %s = %d", token.text.c_str(), ctor_id));

	/* check that this is the right ctor */
	llvm::Value *match_bit = builder.CreateICmpEQ(
			input_ctor_id->get_llvm_value(scope),
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
		bound_var_t::ref member = safe_dyncast<const bound_var_t>(
				extract_member_by_index(
					delegate,
					scope,
					life,
					params[i]->get_location(),
					casted_input,
					casted_input->get_bound_type(),
					i,
					params[i]->token.text,
					false /*as_ref*/));

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
	delegate_t delegate{builder, true};
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
		bound_var_t::ref member = safe_dyncast<const bound_var_t>(
				extract_member_by_index(
					delegate,
					scope,
					life,
					params[i]->get_location(),
					input_value,
					input_value->get_bound_type(),
					i,
					params[i]->token.text,
					false /*as_ref*/));

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
#endif
