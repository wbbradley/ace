#include "zion.h"
#include "patterns.h"
#include "ast.h"
#include "compiler.h"
#include <iostream>
#include "translate.h"

using namespace bitter;

expr_t *build_patterns(
		const defn_id_t &for_defn_id,
		const pattern_blocks_t &pattern_blocks,
		int index,
		const std::unordered_set<std::string> &bound_vars_,
		const translation_env_t &tenv,
		tracked_types_t &typing,
		std::set<defn_id_t> &needed_defns,
		bool &returns,
		identifier_t scrutinee_id,
		types::type_t::ref scrutinee_type,
		types::type_t::ref expected_type)
{
	if (index == pattern_blocks.size()) {
		auto last_block = unit_expr(INTERNAL_LOC());
		typing[last_block] = type_unit(INTERNAL_LOC());
		return last_block;
	} else {
		auto &pattern_block = pattern_blocks[index];

		/* if pattern-matches then let names = {names} in block else build next pattern */
		auto scrutinee_id_with_name_assignment = pattern_block->predicate->instantiate_name_assignment();

		auto bound_vars = bound_vars_;
		bound_vars.insert(scrutinee_id_with_name_assignment.name);

		/* because we have coverage analysis for the patterns, we know we can sometimes skip the
		 * checks, and just do the destructuring. */
		bool do_checks = (index != pattern_blocks.size()-1);

		auto expr = new let_t(
				scrutinee_id_with_name_assignment,
				new var_t(scrutinee_id),
				pattern_block->predicate->translate(
					scrutinee_id_with_name_assignment,
					do_checks,
					bound_vars,
					tenv,
					typing,
					needed_defns,
					returns,
					[&for_defn_id, &pattern_block](const std::unordered_set<std::string> &bound_vars,
						const translation_env_t &tenv,
						tracked_types_t &typing,
						std::set<defn_id_t> &needed_defns,
						bool &returns) -> expr_t * {
						return texpr(
								for_defn_id,
								pattern_block->result,
								bound_vars,
								tenv,
								typing,
								needed_defns,
								returns);
					},
					[index, &pattern_blocks, &for_defn_id, &scrutinee_id, &scrutinee_type, &expected_type](const std::unordered_set<std::string> &bound_vars,
						const translation_env_t &tenv,
						tracked_types_t &typing,
						std::set<defn_id_t> &needed_defns,
						bool &returns) -> expr_t * {
						if (index + 1 < pattern_blocks.size()) {
							return build_patterns(
									for_defn_id,
									pattern_blocks,
									index + 1,
									bound_vars,
									tenv,
									typing,
									needed_defns,
									returns,
									scrutinee_id,
									scrutinee_type,
									expected_type);
						} else {
							assert(false);
							return nullptr;
						}
					}));

		typing[expr] = expected_type;
		return expr;
	}
}

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
		tracked_types_t &typing,
		std::set<defn_id_t> &needed_defns,
		bool &returns)
{
	auto expected_type = tenv.get_type(match);

	debug_above(6, log("match expression is expecting type %s", expected_type->str().c_str()));

	auto scrutinee_expr = texpr(for_defn_id, match->scrutinee, bound_vars, tenv, typing, needed_defns, returns);

	if (returns) {
		throw user_error(scrutinee_expr->get_location(), "this value will return so the match seems pointless?");
	}

	auto scrutinee_type = tenv.get_type(match->scrutinee);

	check_patterns(
			scrutinee_expr->get_location(),
			match->scrutinee->str(),
			tenv,
			match->pattern_blocks,
			scrutinee_type);

	identifier_t scrutinee_id = make_iid("__scrutinee_" + fresh());
	auto new_match = new let_t(
			scrutinee_id,
			scrutinee_expr,
			build_patterns(
				for_defn_id,
				match->pattern_blocks,
				0,
				bound_vars,
				tenv,
				typing,
				needed_defns,
				returns,
				scrutinee_id,
				typing[scrutinee_expr],
				expected_type));
	typing[new_match] = expected_type;
	return new_match;
}

expr_t *literal_t::translate(
		const identifier_t &scrutinee_id,
		bool do_checks,
		const std::unordered_set<std::string> &bound_vars,
		const translation_env_t &tenv,
		tracked_types_t &typing,
		std::set<defn_id_t> &needed_defns,
		bool &returns,
	   	translate_continuation_t &matched,
	   	translate_continuation_t &failed) const
{
	if (!do_checks) {
		return matched(bound_vars, tenv, typing, needed_defns, returns);
	}

	auto type = tenv.get_type(this);
	assert(type != nullptr);

	auto cmp_defn_id = defn_id_t{make_iid("std.=="), type_arrows({type, type, type_id(make_iid(BOOL_TYPE))})->generalize({})};
	auto literal_cmp = new var_t(make_iid(cmp_defn_id.str()));
	typing[literal_cmp] = cmp_defn_id.scheme->instantiate(INTERNAL_LOC());
	needed_defns.insert(cmp_defn_id);

	bool truthy_returns = false;
	bool falsey_returns = false;

	auto cond = new conditional_t(
			new application_t(
				new application_t(
					literal_cmp,
					new var_t(scrutinee_id)),
				new literal_t(token)),
			matched(bound_vars, tenv, typing, needed_defns, truthy_returns),
			failed(bound_vars, tenv, typing, needed_defns, falsey_returns));
	assert(!returns);
	returns = returns || (truthy_returns && falsey_returns);
	return cond;
}

expr_t *translate_next(
				const identifier_t &scrutinee_id,
				bool do_checks,
				const std::unordered_set<std::string> &bound_vars_,
				const std::vector<predicate_t *> &params,
				int param_index,
				int dim_offset,
				const translation_env_t &tenv,
				tracked_types_t &typing,
				std::set<defn_id_t> &needed_defns,
				bool &returns,
			   	translate_continuation_t &matched,
			   	translate_continuation_t &failed)
{
	identifier_t param_id = params[param_index]->instantiate_name_assignment();

	auto bound_vars = bound_vars_;
	bound_vars.insert(param_id.name);

	auto matching = [param_index, dim_offset, &matched, &failed, &params, &scrutinee_id, do_checks](
			const std::unordered_set<std::string> &bound_vars,
			const translation_env_t &tenv,
			tracked_types_t &typing,
			std::set<defn_id_t> &needed_defns,
			bool &returns)
	{
		if (param_index + 1 < params.size()) {
			return translate_next(
					scrutinee_id,
					do_checks,
					bound_vars,
					params,
					param_index + 1,
					dim_offset,
					tenv,
					typing,
					needed_defns,
					returns,
					matched,
					failed);
		} else {
			return matched(bound_vars, tenv, typing, needed_defns, returns);
		}
	};

	return new let_t(
			param_id,
			new application_t(
				new application_t(
					new var_t(make_iid("__builtin_get_dim")),
					new var_t(scrutinee_id)),
				new literal_t(token_t{INTERNAL_LOC(), tk_integer, string_format("%d", param_index + dim_offset)})),
			params[param_index]->translate(
				param_id,
				do_checks,
				bound_vars,
			   	tenv,
			   	typing,
			   	needed_defns,
				returns,
			   	matching,
			   	failed));
}

expr_t *ctor_predicate_t::translate(
		const identifier_t &scrutinee_id,
		bool do_checks,
		const std::unordered_set<std::string> &bound_vars,
		const translation_env_t &tenv,
		tracked_types_t &typing,
		std::set<defn_id_t> &needed_defns,
		bool &returns,
	   	translate_continuation_t &matched,
	   	translate_continuation_t &failed) const
{
	if (do_checks) {
		static auto Int = type_id(make_iid(INT_TYPE));
		auto cmp_defn_id = defn_id_t{make_iid("std.=="), type_arrows({Int, Int, type_id(make_iid(BOOL_TYPE))})->generalize({})};
		auto ctor_id_cmp = new var_t(make_iid(cmp_defn_id.str()));
		typing[ctor_id_cmp] = cmp_defn_id.scheme->instantiate(INTERNAL_LOC());
		needed_defns.insert(cmp_defn_id);

		auto condition = new application_t(
				new application_t(
					ctor_id_cmp,
					new literal_t(token_t{location, tk_integer, "0"})),
				new application_t(
					new var_t(make_iid("__builtin_get_ctor_id")),
					new var_t(scrutinee_id)));

		bool truthy_returns = false;
		bool falsey_returns = false;
		auto cond = new conditional_t(
				condition,
				(params.size() != 0)
				? translate_next(scrutinee_id, do_checks, bound_vars, params, 0, 1 /*dim_offset*/, tenv, typing, needed_defns, truthy_returns, matched, failed)
				: matched(bound_vars, tenv, typing, needed_defns, truthy_returns),
				failed(bound_vars, tenv, typing, needed_defns, falsey_returns));
		assert(!returns);
		returns = returns || (truthy_returns && falsey_returns);
		return cond;
	} else {
		return (params.size() != 0)
			? translate_next(scrutinee_id, do_checks, bound_vars, params, 0, 1 /*dim_offset*/, tenv, typing, needed_defns, returns, matched, failed)
			: matched(bound_vars, tenv, typing, needed_defns, returns);
	}
}

expr_t *tuple_predicate_t::translate(
		const identifier_t &scrutinee_id,
		bool do_checks,
		const std::unordered_set<std::string> &bound_vars,
		const translation_env_t &tenv,
		tracked_types_t &typing,
		std::set<defn_id_t> &needed_defns,
		bool &returns,
	   	translate_continuation_t &matched,
	   	translate_continuation_t &failed) const
{
	return (params.size() != 0)
		? translate_next(scrutinee_id, do_checks, bound_vars, params, 0, 0 /*dim_offset*/, tenv, typing, needed_defns, returns, matched, failed)
		: matched(bound_vars, tenv, typing, needed_defns, returns);
}

expr_t *irrefutable_predicate_t::translate(
		const identifier_t &scrutinee_id,
		bool do_checks,
		const std::unordered_set<std::string> &bound_vars,
		const translation_env_t &tenv,
		tracked_types_t &typing,
		std::set<defn_id_t> &needed_defns,
		bool &returns,
	   	translate_continuation_t &matched,
	   	translate_continuation_t &) const
{
	return matched(bound_vars, tenv, typing, needed_defns, returns);
}
