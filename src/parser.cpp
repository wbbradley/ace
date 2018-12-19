#include <stdlib.h>
#include <string>
#include <iostream>
#include "logger.h"
#include "ast.h"
#include "token.h"
#include "logger_decls.h"
#include "compiler.h"
#include <csignal>
#include "parse_state.h"
#include "parser.h"
#include "type_parser.h"
#include "disk.h"

using namespace bitter;

expr_t *parse_literal(parse_state_t &ps);
expr_t *parse_expr(parse_state_t &ps);
expr_t *parse_assignment(parse_state_t &ps);
expr_t *parse_tuple_expr(parse_state_t &ps);
expr_t *parse_let(parse_state_t &ps, identifier_t var_id, bool is_let);
expr_t *parse_block(parse_state_t &ps, bool expression_means_return);
conditional_t *parse_if(parse_state_t &ps);
while_t *parse_while(parse_state_t &ps);
lambda_t *parse_lambda(parse_state_t &ps);
match_t *parse_match(parse_state_t &ps);
predicate_t *parse_predicate(parse_state_t &ps, bool allow_else, maybe<identifier_t> name_assignment);

bool token_begins_type(const token_t &token) {
	switch (token.tk) {
	case tk_integer:
	case tk_string:
	case tk_times:
	case tk_lsquare:
	case tk_lparen:
	case tk_identifier:
		return true;
	default:
		return false;
	};
}

#if 0
struct scoping_t {
	scoping_t(parse_state_t &ps, bool is_let) :
		ps(ps),
		cur_scope(scope_t{identifier_t::from_token(ps.token), is_let})
	{
		chomp_token(tk_identifier);
		for (const scope_t &scope : ps.scopes) {
			if (cur_scope.id.name == scope.id.name) {
				auto error = user_error(
						cur_scope.id.location,
					   	"duplicate name " c_id("%s") " found",
					   	cur_scope.id.name.c_str());
				error.add_info(scope.id.location, "see prior declaration here");
				throw error;
			}
		}

		ps.scopes.push_back(cur_scope);
	}

	~scoping_t() {
		assert(ps.scopes.back().id.name == cur_scope.id.name);
		ps.scopes.pop_back();
	}

	parse_state_t &ps;
	scope_t cur_scope;
};
#endif

std::vector<std::pair<int, identifier_t>> extract_ids(const std::vector<expr_t*> &dims) {
	std::vector<std::pair<int, identifier_t>> refs;
	int i = 0;
	for (auto dim: dims) {
		if (var_t *var = dcast<var_t*>(dim)) {
			if (var->id.name != "_") {
				refs.push_back({i, var->id});
			}
		} else {
			throw user_error(dim->get_location(), "only reference expressions are allowed here");
		}
		++i;
	}
	assert(refs.size() != 0);
	return refs;
}

expr_t *parse_assign_tuple(parse_state_t &ps, tuple_t *tuple) {
	eat_token();
	auto rhs = parse_expr(ps);
	auto rhs_var = new var_t({fresh(), rhs->get_location()});

	std::vector<std::pair<int, identifier_t>> refs = extract_ids(tuple->dims);
	if (refs.size() == 0) {
		throw user_error(ps.token.location, "nothing to destructure");
	}


	expr_t *body = parse_block(ps, false/*expression_means_return*/);
	for (int i=refs.size()-1; i >= 0; --i) {
		body = new let_t(
				refs[i].second,
				new application_t(
					new var_t(make_iid(string_format("__[%d]__", i))),
					rhs_var),
				body);
	}

	return new let_t(rhs_var->id, rhs, body);
}

expr_t *parse_var_decl(parse_state_t &ps, bool is_let, bool allow_tuple_destructuring) {
	if (ps.token.tk == tk_lparen) {
		if (!is_let) {
			throw user_error(ps.token.location, "mutable tuple destructuring is not yet impl");
		}
		if (!allow_tuple_destructuring) {
			throw user_error(ps.token.location, "tuple destructuring is not allowed here");
		}

		auto prior_token = ps.token;
		auto tuple = dcast<tuple_t*>(parse_tuple_expr(ps));
		if (tuple == nullptr) {
			throw user_error(prior_token.location, "tuple destructuring detected an invalid expression on the lhs");
		}

		if (ps.token.tk == tk_assign) {
			return parse_assign_tuple(ps, tuple);
		} else {
			throw user_error(ps.token.location, "destructured tuples must be assigned to an rhs");
		}
	} else {
		expect_token(tk_identifier);

		identifier_t var_id = identifier_t::from_token(ps.token);
		ps.advance();
		return parse_let(ps, var_id, is_let);
	}
}

expr_t *parse_let(parse_state_t &ps, identifier_t var_id, bool is_let) {
	expr_t *initializer = nullptr;

	if (!ps.line_broke() && (ps.token.tk == tk_assign || ps.token.tk == tk_becomes)) {
		eat_token();
		initializer = parse_expr(ps);
	} else {
		initializer = new application_t(
				new var_t(make_iid("__init__")),
				new var_t(make_iid("unit")));
	}

	if (!is_let) {
		initializer = new application_t(new var_t(make_iid("Ref")), initializer);
	}

	return new let_t(var_id, initializer, parse_block(ps, false /*expression_means_return*/));
}

expr_t *parse_return_statement(parse_state_t &ps) {
	auto return_token = ps.token;
	chomp_ident(K(return));
	return new return_statement_t(
			(!ps.line_broke() && ps.token.tk != tk_rcurly)
			? parse_expr(ps)
			: new var_t(make_iid("unit")));
}

maybe<identifier_t> parse_with_param(parse_state_t &ps, expr_t *&expr) {
	expr = parse_expr(ps);
	if (auto var = dcast<var_t *>(expr)) {
		if (ps.token.tk == tk_becomes) {
			auto param_id = var->id;
			ps.advance();
			expr = parse_expr(ps);
			return maybe<identifier_t>(param_id);
		}
	}
	return maybe<identifier_t>();
}

expr_t *parse_with_block(parse_state_t &ps) {
	return new var_t(identifier_t{"unit", INTERNAL_LOC()});
#if 0
	auto with_token = ps.token;
	ps.advance();

	expr_t *expr = nullptr;
	maybe<identifier_t> maybe_param_id = parse_with_param(ps, expr);
	assert(expr != nullptr);

	identifier_t param_id = (maybe_param_id.valid
	   	? maybe_param_id.t
	   	: identifier_t{fresh(), with_token.location});

	auto block = parse_block(ps, false /*expression_means_return*/);

	auto else_token = ps.token;
	chomp_ident(K(else));

	identifier_t error_var_id = (ps.token.tk == tk_identifier)
		? identifier_t::from_token(ps.token_and_advance())
		: identifier_t{fresh(), with_token.location};

	auto error_block = parse_block(ps, false /* expression_means_return */);

	auto cleanup_token = identifier_t{"__cleanup", with_token.location};
	auto match = create<match_expr_t>(with_token);
	match->value = expr;

	auto with_pattern = create<pattern_block_t>(with_token);
	with_pattern->block = block;

	auto with_predicate = create<ctor_predicate_t>(token_t{with_token.location, tk_identifier, "Acquired"});
	with_predicate->params.push_back(create<irrefutable_predicate_t>(param_id));
	with_predicate->params.push_back(create<irrefutable_predicate_t>(cleanup_token));
	with_pattern->predicate = with_predicate;

	auto cleanup_defer = create<defer_t>(block->token);
	cleanup_defer->callable = create<reference_expr_t>(cleanup_token);
	block->statements.insert(block->statements.begin(), cleanup_defer);

	auto else_pattern = create<pattern_block_t>(else_token);
	else_pattern->block = error_block;

	auto else_predicate = create<ctor_predicate_t>(token_t{else_token.location, tk_identifier, "Failed"});
	else_predicate->params.push_back(create<irrefutable_predicate_t>(error_var_id));
	else_pattern->predicate = else_predicate;

	match->pattern_blocks.push_back(with_pattern);
	match->pattern_blocks.push_back(else_pattern);
	return match;
#endif
}

expr_t *wrap_with_iter(expr_t *expr) {
	return new application_t(
			new var_t(identifier_t{"iter", expr->get_location()}),
			expr);
}

expr_t *parse_for_block(parse_state_t &ps) {
	return new var_t(identifier_t{"unit", INTERNAL_LOC()});
#if 0
	auto for_token = ps.token;
	ps.advance();

	token_t param_id;
	std::shared_ptr<tuple_expr_t> tuple_expr;

	if (ps.token.tk == tk_lparen) {
		tuple_expr = dyncast<tuple_expr_t>(tuple_expr_t::parse(ps));
		if (tuple_expr == nullptr) {
			throw user_error(ps.token.location, "expected a tuple of variable names");
		}
	} else {
		expect_token(tk_identifier);
		param_id = ps.token;
		ps.advance();
	}

	token_t becomes_token;

	chomp_ident(K(in));

	auto expr = expression_t::parse(ps);
	auto block = block_t::parse(ps, false /*expression_means_return*/);

	/* create the iterator function by evaluating the `iterable` (for _ in `iterable` { ... }) */
	auto iter_func_decl = create<var_decl_t>(token_t{expr->get_location(), tk_identifier, types::gensym(INTERNAL_LOC())->get_name()});
	iter_func_decl->is_let_var = true;
	iter_func_decl->parsed_type = parsed_type_t(type_variable(expr->get_location()));
	iter_func_decl->initializer = wrap_with_iter(expr);

	/* call the iterator value (which is a function returned by the expression */
	auto iter_token = token_t{expr->get_location(), tk_identifier, iter_func_decl->token.text};
	auto iter_ref = create<reference_expr_t>(iter_token);
	auto iter_callsite = create<callsite_expr_t>(iter_token);
	iter_callsite->function_expr = iter_ref;

	auto just_pattern = create<pattern_block_t>(for_token);
	just_pattern->block = block;

	token_t just_value_token = token_t{for_token.location, tk_identifier, types::gensym(INTERNAL_LOC())->get_name()};
	if (tuple_expr != nullptr) {
		auto destructured_tuple_decl = ast::create<ast::destructured_tuple_decl_t>(tuple_expr->token);
		destructured_tuple_decl->is_let = false;
		destructured_tuple_decl->lhs = tuple_expr;
		assert(destructured_tuple_decl->lhs != nullptr);
		destructured_tuple_decl->parsed_type = parsed_type_t(type_variable(tuple_expr->token.location));
		destructured_tuple_decl->initializer = create<reference_expr_t>(just_value_token);

		just_pattern->block->statements.insert(just_pattern->block->statements.begin(), destructured_tuple_decl);
	} else {
		auto just_var_decl = create<var_decl_t>(param_id);
		just_var_decl->is_let_var = false;
		just_var_decl->parsed_type = parsed_type_t(type_variable(param_id.location));
		just_var_decl->initializer = create<reference_expr_t>(just_value_token);

		just_pattern->block->statements.insert(just_pattern->block->statements.begin(), just_var_decl);
	}

	auto just_predicate = create<ctor_predicate_t>(token_t{for_token.location, tk_identifier, "Just"});
	just_predicate->params.push_back(create<irrefutable_predicate_t>(just_value_token));
	just_pattern->predicate = just_predicate;

	auto break_block = create<block_t>(for_token);
	break_block->statements.push_back(create<break_flow_t>(for_token));

	auto nothing_pattern = create<pattern_block_t>(for_token);
	nothing_pattern->block = break_block;

	auto nothing_predicate = create<ctor_predicate_t>(token_t{for_token.location, tk_identifier, "Nothing"});
	nothing_pattern->predicate = nothing_predicate;

	auto match = create<match_expr_t>(for_token);
	match->value = iter_callsite;
	match->pattern_blocks.push_back(just_pattern);
	match->pattern_blocks.push_back(nothing_pattern);

	auto while_block = create<block_t>(block->token);
	while_block->statements.push_back(match);

	auto while_loop = create<while_block_t>(for_token);
	while_loop->block = while_block;
	while_loop->condition = create<reference_expr_t>(token_t{becomes_token.location, tk_identifier, "true"});

	std::shared_ptr<block_t> outer_block = create<block_t>(for_token);
	outer_block->statements.push_back(iter_func_decl);
	outer_block->statements.push_back(while_loop);
	// log_location(log_info, outer_block->get_location(), "created %s", outer_block->str().c_str());
	return outer_block;
#endif
}

expr_t *parse_defer(parse_state_t &ps) {
	return new var_t(identifier_t{"unit", INTERNAL_LOC()});
#if 0
	auto defer = create<defer_t>(ps.token);
	ps.advance();
	defer->callable = expression_t::parse(ps);
	return defer;
#endif
}

expr_t *parse_new_expr(parse_state_t &ps) {
	expr_t *init = new application_t(
			new var_t({"__init__", ps.token.location}),
			new var_t({"unit", ps.token.location}));
	ps.advance();
	try {
		types::type_t::ref type = types::parse_type(ps, {});
		return new as_t(init, type);
	} catch (user_error &e) {
		std::throw_with_nested(user_error(init->get_location(), "while parsing unary operator new"));
	}
}

expr_t *parse_statement(parse_state_t &ps) {
	assert(ps.token.tk != tk_rcurly);

	if (ps.token.is_ident(K(var))) {
		ps.advance();
		return parse_var_decl(ps, false /*is_let*/, true /*allow_tuple_destructuring*/);
	} else if (ps.token.is_ident(K(let))) {
		ps.advance();
		return parse_var_decl(ps, true /*is_let*/, true /*allow_tuple_destructuring*/);
	} else if (ps.token.is_ident(K(if))) {
		return parse_if(ps);
	} else if (ps.token.is_ident(K(while))) {
		return parse_while(ps);
	} else if (ps.token.is_ident(K(for))) {
		return parse_for_block(ps);
	//} else if (ps.token.is_ident(K(match))) {
		//return parse_match(ps);
	// } else if (ps.token.is_ident(K(with))) {
		// return parse_with_block(ps);
	} else if (ps.token.is_ident(K(new))) {
		return parse_new_expr(ps);
	} else if (ps.token.is_ident(K(fn))) {
		ps.advance();
		if (ps.token.tk == tk_identifier) {
			return new let_t(
					identifier_t::from_token(ps.token),
				   	parse_lambda(ps),
				   	parse_block(ps, false /*expression_means_return*/));
		} else {
			return parse_lambda(ps);
		}
	} else if (ps.token.is_ident(K(return))) {
		return parse_return_statement(ps);
	} else if (ps.token.is_ident(K(unreachable))) {
		return new var_t(iid(ps.token));
	// } else if (ps.token.is_ident(K(type))) {
		// return parse_type_def_t::parse(ps);
	// } else if (ps.token.is_ident(K(defer))) {
		// return defer_t::parse(ps);
	} else if (ps.token.is_ident(K(continue))) {
		return new continue_t(ps.token_and_advance().location);
	} else if (ps.token.is_ident(K(break))) {
		return new break_t(ps.token_and_advance().location);
	} else {
		return parse_assignment(ps);
	}
}

expr_t *parse_var_ref(parse_state_t &ps) {
	// TODO: if this name is a var, then treat it as a load
	if (ps.token.tk != tk_identifier) {
		throw user_error(ps.token.location, "expected an identifier");
	}

	if (ps.token.is_ident(K(__filename__))) {
		auto token = ps.token_and_advance();
		return new literal_t(token_t{token.location, tk_string, escape_json_quotes(token.location.filename)});
	}

	return new var_t(identifier_t::from_token(ps.token_and_advance()));
}

expr_t *parse_base_expr(parse_state_t &ps) {
	if (ps.token.tk == tk_lparen) {
		return parse_tuple_expr(ps);
	} else if (ps.token.is_ident(K(new))) {
		return parse_new_expr(ps);
	} else if (ps.token.is_ident(K(fn))) {
		ps.advance();
		return parse_lambda(ps);
	// } else if (ps.token.is_ident(K(match))) {
		// return parse_match(ps);
	} else if (ps.token.tk == tk_identifier) {
		return parse_var_ref(ps);
	} else {
		return parse_literal(ps);
	}
}

expr_t *parse_array_literal(parse_state_t &ps) {
	location_t location = ps.token.location;
	chomp_token(tk_lsquare);
	std::vector<expr_t*> exprs;

	auto array_var = new var_t(identifier_t{fresh(), location});

	int i = 0;
	while (ps.token.tk != tk_rsquare && ps.token.tk != tk_none) {
		++i;
		exprs.push_back(
				new application_t(
					new application_t(
						new var_t(identifier_t{"append", ps.token.location}),
						array_var),
					parse_expr(ps)));

		if (ps.token.tk == tk_comma) {
			ps.advance();
		} else if (ps.token.tk != tk_rsquare) {
			throw user_error(ps.token.location, "found something that does not make sense in an array literal");
		}
	}
	chomp_token(tk_rsquare);
	exprs.push_back(array_var);

	return new let_t(
			array_var->id,
			new application_t(
				new var_t(identifier_t{"__init_vector__", location}),
				new literal_t(token_t{location, tk_integer, string_format("%d", exprs.size())})),
			new block_t(exprs));
}

expr_t *parse_literal(parse_state_t &ps) {
	switch (ps.token.tk) {
	case tk_integer:
	case tk_string:
	case tk_char:
	case tk_float:
		return new literal_t(ps.token_and_advance());
	case tk_lsquare:
		return parse_array_literal(ps);
	// case tk_lcurly:
	//	return assoc_array_expr_t::parse(ps);

	case tk_identifier:
		throw user_error(ps.token.location, "unexpected token found when parsing literal expression. '" c_error("%s") "'", ps.token.text.c_str());

	default:
		if (ps.token.tk == tk_lcurly) {
			throw user_error(ps.token.location, "this squiggly brace is a surprise");
		} else if (ps.lexer.eof()) {
			auto error = user_error(ps.token.location, "unexpected end-of-file.");
			for (auto pair : ps.lexer.nested_tks) {
				error.add_info(pair.first, "unclosed %s here", tkstr(pair.second));
			}
			throw error;
		} else {
			throw user_error(ps.token.location, "out of place token found when parsing literal expression. '" c_error("%s") "' (%s)",
					ps.token.text.c_str(),
					tkstr(ps.token.tk));
		}
	}
}

expr_t *parse_postfix_expr(parse_state_t &ps) {
	expr_t *expr = parse_base_expr(ps);

	while (!ps.line_broke() &&
			(ps.token.tk == tk_lsquare ||
			 ps.token.tk == tk_lparen ||
			 ps.token.tk == tk_dot ||
			 ps.token.tk == tk_bang))
	{
		switch (ps.token.tk) {
		case tk_lparen:
			{
				/* function call */
				auto location = ps.token.location;
				ps.advance();
				if (ps.token.tk == tk_rparen) {
					ps.advance();
					expr = new application_t(expr, new var_t(identifier_t{"unit", INTERNAL_LOC()}));
				} else {
					while (ps.token.tk != tk_rparen) {
						expr = new application_t(expr, parse_expr(ps));
						if (ps.token.tk == tk_comma) {
							ps.advance();
						} else {
							expect_token(tk_rparen);
						}
					}
					ps.advance();
				}
				break;
			}
#if 0
		case tk_dot:
			{
				auto dot_expr = create<ast::dot_expr_t>(ps.token);
				eat_token();
				expect_token(tk_identifier);
				dot_expr->rhs = ps.token;
				ps.advance();
				dot_expr->lhs.swap(expr);
				assert(expr == nullptr);
				expr = dot_expr;
				break;
			}
#endif
		case tk_lsquare:
			{
				ps.advance();
				bool is_slice = false;

				expr_t *start = parse_expr(ps);
				if (ps.token.tk == tk_colon) {
					is_slice = true;
					ps.advance();
				} else {
					chomp_token(tk_rsquare);
				}

				if (ps.token.tk == tk_rsquare) {
					expr = new application_t(
							new application_t(
								new var_t(identifier_t{is_slice ? "__getslice__/2" : "__getitem__", ps.token.location}),
								expr),
							start);
				} else {
					expr_t *stop = parse_expr(ps);
					chomp_token(tk_rsquare);

					assert(is_slice);
					expr = new application_t(
							new application_t(
								new application_t(
									new var_t(identifier_t{"__getslice__/3", ps.token.location}),
									expr),
								start),
							stop);
				}
				break;
			}
		default:
			break;
		}
	}

	return expr;
}

expr_t *parse_prefix_expr(parse_state_t &ps) {
	maybe<token_t> prefix = 
		(ps.token.tk == tk_minus || ps.token.tk == tk_plus || ps.token.is_ident(K(not)))
	   	? maybe<token_t>(ps.token)
	   	: maybe<token_t>();

	if (prefix.valid) {
		ps.advance();
	}

	expr_t *rhs;
	if (ps.token.is_ident(K(not)) || ps.token.tk == tk_minus || ps.token.tk == tk_plus) {
		/* recurse to find more prefix expressions */
		rhs = parse_prefix_expr(ps);
	} else {
		/* ok, we're done with prefix operators */
		rhs = parse_postfix_expr(ps);
	}

	if (prefix.valid) {
		return new application_t(
				new var_t(identifier_t{prefix.t.text, prefix.t.location}),
				rhs);
	} else {
	   	return rhs;
   	}
}

expr_t *parse_times_expr(parse_state_t &ps) {
	expr_t *expr = parse_prefix_expr(ps);

	while (!ps.line_broke() && (ps.token.tk == tk_times
				|| ps.token.tk == tk_divide_by
				|| ps.token.tk == tk_mod)) {
		identifier_t op{ps.token.text, ps.token.location};
		ps.advance();

		expr = new application_t(
				new application_t(
					new var_t(op),
					expr),
				parse_prefix_expr(ps));
	}

	return expr;
}

expr_t *parse_plus_expr(parse_state_t &ps) {
	auto expr = parse_times_expr(ps);

	while (!ps.line_broke() &&
			(ps.token.tk == tk_plus || ps.token.tk == tk_minus || ps.token.tk == tk_backslash))
	{
		identifier_t op{ps.token.text, ps.token.location};
		ps.advance();

		expr = new application_t(
				new application_t(
					new var_t(op),
					expr),
				parse_times_expr(ps));
	}

	return expr;
}

expr_t *parse_shift_expr(parse_state_t &ps) {
	auto expr = parse_plus_expr(ps);

	while (!ps.line_broke() &&
		   	(ps.token.tk == tk_shift_left || ps.token.tk == tk_shift_right))
	{
		identifier_t op{ps.token.text, ps.token.location};
		ps.advance();

		expr = new application_t(
				new application_t(
					new var_t(op),
					expr),
				parse_plus_expr(ps));
	}

	return expr;
}

expr_t *parse_binary_eq_expr(parse_state_t &ps) {
	auto lhs = parse_shift_expr(ps);
	if (ps.line_broke()
			|| !(ps.token.tk == tk_binary_equal
				|| ps.token.tk == tk_binary_inequal))
	{
		/* there is no rhs */
		return lhs;
	}

	identifier_t op{ps.token.text, ps.token.location};
	ps.advance();

	return new application_t(
			new application_t(
				new var_t(op),
				lhs),
			parse_shift_expr(ps));
}

expr_t *parse_ineq_expr(parse_state_t &ps) {
	auto lhs = parse_binary_eq_expr(ps);
	if (ps.line_broke()
			|| !(ps.token.tk == tk_gt
				|| ps.token.tk == tk_gte
				|| ps.token.tk == tk_lt
				|| ps.token.tk == tk_lte)) {
		/* there is no rhs */
		return lhs;
	}

	identifier_t op{ps.token.text, ps.token.location};
	ps.advance();

	return new application_t(
			new application_t(
				new var_t(op),
				lhs),
			parse_shift_expr(ps));
}

expr_t *parse_eq_expr(parse_state_t &ps) {
	auto lhs = parse_ineq_expr(ps);
	bool not_in = false;
	if (ps.token.is_ident(K(not))) {
		eat_token();
		expect_ident(K(in));
		not_in = true;
	}

	if (ps.line_broke() ||
			!(ps.token.is_ident(K(in))
				|| ps.token.tk == tk_equal
				|| ps.token.tk == tk_inequal)) {
		/* there is no rhs */
		return lhs;
	}

	identifier_t op{not_in ? "not-in" : ps.token.text, ps.token.location};
	ps.advance();

	return new application_t(
			new application_t(
				new var_t(op),
				lhs),
			parse_ineq_expr(ps));
}

expr_t *parse_bitwise_and(parse_state_t &ps) {
	auto expr = parse_eq_expr(ps);

	while (!ps.line_broke() && ps.token.tk == tk_ampersand) {

		identifier_t op{ps.token.text, ps.token.location};
		ps.advance();

		expr = new application_t(
				new application_t(
					new var_t(op),
					expr),
				parse_eq_expr(ps));
	}

	return expr;
}

expr_t *parse_bitwise_xor(parse_state_t &ps) {
	auto expr = parse_bitwise_and(ps);

	while (!ps.line_broke() && ps.token.tk == tk_hat) {
		identifier_t op{ps.token.text, ps.token.location};
		ps.advance();

		expr = new application_t(
				new application_t(
					new var_t(op),
					expr),
				parse_bitwise_and(ps));
	}
	return expr;
}

expr_t *parse_bitwise_or(parse_state_t &ps) {
	auto expr = parse_bitwise_xor(ps);

	while (!ps.line_broke() && ps.token.tk == tk_pipe) {
		identifier_t op{ps.token.text, ps.token.location};
		ps.advance();

		expr = new application_t(
				new application_t(
					new var_t(op),
					expr),
				parse_bitwise_xor(ps));
	}

	return expr;
}

expr_t *parse_and_expr(parse_state_t &ps) {
	auto expr = parse_bitwise_or(ps);

	while (!ps.line_broke() && (ps.token.is_ident(K(and)))) {
		identifier_t op{ps.token.text, ps.token.location};
		ps.advance();

		expr = new application_t(
				new application_t(
					new var_t(op),
					expr),
				parse_bitwise_or(ps));
	}

	return expr;
}

expr_t *parse_tuple_expr(parse_state_t &ps) {
	auto start_token = ps.token;
	chomp_token(tk_lparen);
    if (ps.token.tk == tk_rparen) {
		/* we've got a reference to sole value of unit type */
        return new var_t(identifier_t{"unit", ps.token_and_advance().location});
    }
	expr_t *expr = parse_expr(ps);
	if (ps.token.tk != tk_comma) {
		chomp_token(tk_rparen);
		return expr;
	} else {
		ps.advance();

		std::vector<expr_t *> exprs;

		exprs.push_back(expr);

		/* now let's find the rest of the values */
		while (ps.token.tk != tk_rparen) {
			exprs.push_back(parse_expr(ps));
			if (ps.token.tk == tk_comma) {
				ps.advance();
			} else {
				chomp_token(tk_rparen);
			}
			// continue and read the next parameter
		}
		chomp_token(tk_rparen);

		return new tuple_t(start_token.location, exprs);
	}
}

expr_t *parse_or_expr(parse_state_t &ps) {
	expr_t *expr = parse_and_expr(ps);

	while (!ps.line_broke() && (ps.token.is_ident(K(or)))) {
		identifier_t op{ps.token.text, ps.token.location};
		ps.advance();

		expr = new application_t(
				new application_t(
					new var_t(op),
					expr),
				parse_and_expr(ps));
	}

	return expr;
}

expr_t *parse_ternary_expr(parse_state_t &ps) {
	expr_t *condition = parse_or_expr(ps);
	if (ps.token.tk == tk_maybe) {
		ps.advance();

		expr_t *truthy_expr = parse_or_expr(ps);
		expect_token(tk_colon);
		ps.advance();
		return new conditional_t(
				condition,
				truthy_expr,
				parse_expr(ps));
	} else {
		return condition;
	}
}

expr_t *parse_expr(parse_state_t &ps) {
	return parse_ternary_expr(ps);
}

expr_t *parse_assignment(parse_state_t &ps) {
	// (store! x y)
	expr_t *lhs = parse_expr(ps);

	if (!ps.line_broke() && ps.token.tk == tk_assign) {
		ps.advance();
		expr_t *rhs = parse_expr(ps);
		return new application_t(
				new application_t(
					new var_t(identifier_t{std::string{"store!"}, ps.token.location}),
					lhs),
				rhs);
	}

	if (!ps.line_broke() && ps.token.tk == tk_becomes) {
		if (var_t *var = dcast<var_t *>(lhs)) {
			return parse_let(ps, var->id, true /* is_let */);
		} else if (auto tuple = dcast<tuple_t *>(lhs)) {
			return parse_assign_tuple(ps, tuple);
		} else {
			throw user_error(ps.token.location, ":= may only come after a new symbol name");
		}
	} else {
		return lhs;
	}
}

expr_t *parse_block(parse_state_t &ps, bool expression_means_return) {
	bool expression_block_syntax = false;
	token_t expression_block_assign_token;
	bool finish_block = false;
	if (ps.token.tk == tk_lcurly) {
		finish_block = true;
		ps.advance();
	} else if (ps.token.tk == tk_expr_block) {
		expression_block_syntax = true;
		expression_block_assign_token = ps.token;
		ps.advance();
	}

	if (expression_block_syntax) {
		if (!ps.line_broke()) {
			auto statement = parse_statement(ps);
			if (expression_means_return) {
				if (auto expression = dcast<expr_t*>(statement)) {
					auto return_statement = new return_statement_t(expression);
					statement = return_statement;
				}
			}

			if (ps.token.tk != tk_rparen
					&& ps.token.tk != tk_rcurly
					&& ps.token.tk != tk_rsquare
					&& ps.token.tk != tk_comma
					&& !ps.line_broke())
			{
				throw user_error(ps.token.location, "this looks hard to read. you should have a line break after = blocks, unless they are immediately followed by one of these: )]}");
			}
			return statement;
		} else {
			throw user_error(ps.token.location, "empty expression blocks are not allowed");
		}
	} else {
		std::vector<expr_t*> stmts;
		while (ps.token.tk != tk_rcurly) {
			while (ps.token.tk == tk_semicolon) {
				ps.advance();
			}
			stmts.push_back(parse_statement(ps));
			while (ps.token.tk == tk_semicolon) {
				ps.advance();
			}
		}
		if (finish_block) {
			chomp_token(tk_rcurly);
		}
		if (stmts.size() == 0) {
			return new var_t(identifier_t{"unit", ps.token.location});
		} else if (stmts.size() == 1) {
			return stmts[0];
		} else {
			return new block_t(stmts);
		}
	}
}

conditional_t *parse_if(parse_state_t &ps) {
	if (ps.token.is_ident(K(if))) {
		ps.advance();
	} else {
		throw user_error(ps.token.location, "expected if");
	}

	token_t condition_token = ps.token;
	expr_t *condition = parse_expr(ps);
	expr_t *block = parse_block(ps, false /*expression_means_return*/);
	expr_t *else_ = nullptr;
	/* check the successive instructions for "else if" or else */
	if (ps.token.is_ident(K(else))) {
		ps.advance();
		if (ps.token.is_ident(K(if))) {
			if (ps.line_broke()) {
				throw user_error(ps.token.location, "else if must be on the same line");
			}
			else_ = parse_if(ps);
		} else {
			else_ = parse_block(ps, false /*expression_means_return*/);
		}
	}

	return new conditional_t(condition, block, else_);
}

while_t *parse_while(parse_state_t &ps) {
	auto while_token = ps.token;
	chomp_ident(K(while));
	token_t condition_token = ps.token;
	if (condition_token.is_ident(K(match))) {
		/* sugar for while match ... which becomes while true { match ... } */
		return new while_t(
				new var_t(identifier_t{"true", while_token.location}),
				parse_match(ps));
	} else {
		return new while_t(parse_expr(ps), parse_block(ps, false /*expression_means_return*/));
	}
}

predicate_t *parse_ctor_predicate(parse_state_t &ps, maybe<identifier_t> name_assignment) {
	assert(ps.token.tk == tk_identifier && isupper(ps.token.text[0]));
	identifier_t ctor_name = identifier_t::from_token(ps.token);

	ps.advance();

	std::vector<predicate_t *> params;
	if (ps.token.tk == tk_lparen) {
		ps.advance();
		while (ps.token.tk != tk_rparen) {
			params.push_back(parse_predicate(ps, false /*allow_else*/, maybe<identifier_t>() /*name_assignment*/));
			if (ps.token.tk != tk_rparen) {
				chomp_token(tk_comma);
			}
		}
		chomp_token(tk_rparen);
	}
	return new ctor_predicate_t(
			ctor_name.location,
			params,
			ctor_name,
		   	name_assignment);
}

predicate_t *parse_tuple_predicate(parse_state_t &ps, maybe<identifier_t> name_assignment) {
	assert(ps.token.tk == tk_lparen);
	ps.advance();

	std::vector<predicate_t *> params;
	if (ps.token.tk == tk_lparen) {
		ps.advance();
		while (ps.token.tk != tk_rparen) {
			params.push_back(parse_predicate(ps, false /*allow_else*/, maybe<identifier_t>() /*name_assignment*/));
			if (ps.token.tk != tk_rparen) {
				chomp_token(tk_comma);
			}
		}
		chomp_token(tk_rparen);
	}
	return new tuple_predicate_t(
			ps.token.location,
			params,
		   	name_assignment);
}

predicate_t *parse_predicate(parse_state_t &ps, bool allow_else, maybe<identifier_t> name_assignment) {
	if (ps.token.is_ident(K(else))) {
		if (!allow_else) {
			throw user_error(ps.token.location, "illegal keyword " c_type("%s") " in a pattern match context",
					ps.token.text.c_str());
		}
	} else if (is_restricted_var_name(ps.token.text)) {
		throw user_error(ps.token.location, "irrefutable predicates are restricted to non-keyword symbols");
	}

	if (ps.token.tk == tk_lparen) {
		return parse_tuple_predicate(ps, name_assignment);
	} else if (ps.token.tk == tk_identifier) {
		if (isupper(ps.token.text[0])) {
			/* match a ctor */
			return parse_ctor_predicate(ps, name_assignment);
		} else {
			if (name_assignment.valid) {
				throw user_error(ps.token.location, "pattern name assignment is only allowed once per term");
			} else {
				/* match anything */
				auto symbol = identifier_t::from_token(ps.token);
				ps.advance();
				if (ps.token.tk == tk_about) {
					ps.advance();

					return parse_predicate(
							ps,
						   	allow_else,
						   	maybe<identifier_t>(symbol));
				} else {
					return new irrefutable_predicate_t(symbol.location, symbol);
				}
			}
		}
	} else {
		if (name_assignment.valid) {
			throw user_error(ps.token.location, "pattern name assignment is only allowed for data constructor matching");
		}

		std::string sign;
		switch (ps.token.tk) {
		case tk_minus:
		case tk_plus:
			sign = ps.token.text;
			ps.advance();
			if (ps.token.tk != tk_integer && ps.token.tk != tk_float) {
				throw user_error(ps.prior_token.location, "unary prefix %s is not allowed before %s in this context",
						ps.prior_token.text.c_str(),
						ps.token.text.c_str());
			}
			break;
		default:
			break;
		}

		switch (ps.token.tk) {
		case tk_string:
		case tk_char:
			{
				/* match a literal */
				return new literal_t(ps.token_and_advance());
			}
		case tk_integer:
		case tk_float:
			{
				/* match a literal */
				predicate_t *literal = new literal_t(
						sign != ""
						? token_t(ps.token.location, ps.token.tk, sign + ps.token.text)
						: ps.token);
				ps.advance();
				return literal;
			}
		default:
			throw user_error(ps.token.location, "unexpected token for pattern " c_warn("%s"),
					ps.token.text.c_str());
		}
		return null_impl();
	}
}

pattern_block_t *parse_pattern_block(parse_state_t &ps) {
	return new pattern_block_t(
			parse_predicate(ps, true /*allow_else*/, maybe<identifier_t>() /*name_assignment*/),
			parse_block(ps, false /*expression_means_return*/));
}

match_t *parse_match(parse_state_t &ps) {
	assert(false);
	return nullptr;

#if 0
	auto when_block = create<ast::match_expr_t>(ps.token);
	chomp_ident(K(match));
	bool auto_else = false;
	token_t bang_token = ps.token;
	if (ps.token.tk == tk_bang) {
		auto_else = true;
		ps.advance();
	}
	when_block->value = expression_t::parse(ps);
	chomp_token(tk_lcurly);
	while (ps.token.tk != tk_rcurly) {
		if (ps.token.is_ident(K(else))) {
			throw user_error(ps.token.location, "place else patterns outside of the match block. (match ... { ... } else { ... })");
		}
		when_block->pattern_blocks.push_back(pattern_block_t::parse(ps));
	}
	chomp_token(tk_rcurly);
	if (auto_else) {
		auto pattern_block = ast::create<ast::pattern_block_t>(bang_token);
		pattern_block->predicate = ast::create<ast::irrefutable_predicate_t>(token_t{bang_token.location, tk_identifier, "else"});
		pattern_block->block = ast::create<ast::block_t>(bang_token);
		when_block->pattern_blocks.push_back(pattern_block);
	}
   	if (ps.token.is_ident(K(else))) {
		if (auto_else) {
			throw user_error(ps.token.location, "no need for else block when you are using \"match!\". either delete the ! or discard the else block");
		} else {
			when_block->pattern_blocks.push_back(pattern_block_t::parse(ps));
		}
	}

	if (when_block->pattern_blocks.size() == 0) {
		throw user_error(ps.token.location, "when block did not have subsequent patterns to match");
	}

	return when_block;
#endif
}

identifier_t parse_lambda_param(parse_state_t &ps) {
	if (ps.token.tk == tk_lparen) {
		ps.advance();
		if (ps.token.tk == tk_identifier) {
			return iid(ps.token_and_advance());
		} else if (ps.token.tk == tk_rparen) {
			return identifier_t{"_", ps.token.location};
		}
	} else if (ps.token.tk == tk_comma) {
		ps.advance();
		if (ps.token.tk == tk_identifier) {
			return iid(ps.token_and_advance());
		}
	}

	throw user_error(ps.token.location, "missing parameter name");
}

// TODO: put type mappings into the scope
lambda_t *parse_lambda(parse_state_t &ps) {
	if (ps.token.tk == tk_identifier) {
		throw user_error(ps.token.location, "identifiers are unexpected here");
	}

	if (ps.token.tk == tk_lsquare) {
		throw user_error(ps.token.location, "not yet impl");
	}

	identifier_t param_id = parse_lambda_param(ps);

	if (ps.token.tk != tk_comma && ps.token.tk != tk_rparen) {
		auto type = types::parse_type(ps, {});
		log_location(log_info, type->get_location(), "discarding parsed param type %s", type->str().c_str());
	}

	if (ps.token.tk == tk_comma) {
		return new lambda_t(param_id, parse_lambda(ps));
	} else if (ps.token.tk == tk_rparen) {
		ps.advance();

		return new lambda_t(param_id, parse_block(ps, true /*expression_means_return*/));
	} else {
		throw user_error(ps.token.location, "unexpected token");
	}
}

void parse_maybe_type_decl(parse_state_t &ps, identifiers_t &type_variables) {
	while (!ps.line_broke() && ps.token.tk == tk_identifier) {
		if (token_is_illegal_in_type(ps.token)) {
			if (ps.token.is_ident(K(any))) {
				throw user_error(ps.token.location, "`any` is unnecessary within type parameters of type declarations");
			}

			break;
		}

		/* we found a type variable, let's stash it */
		type_variables.push_back(iid(ps.token));
		ps.advance();
	}
}

#if 0
type_decl_t::ref type_decl_t::parse(parse_state_t &ps, token_t name_token) {
	identifiers_t type_variables;
	parse_maybe_type_decl(ps, type_variables);
	return create<ast::type_decl_t>(name_token, type_variables);
}

std::shared_ptr<type_def_t> type_def_t::parse(parse_state_t &ps) {
	chomp_ident(K(type));
	expect_token(tk_identifier);
	auto type_name_token = ps.token;
	ps.advance();

	auto type_def = create<ast::type_def_t>(type_name_token);
	type_def->type_decl = type_decl_t::parse(ps, type_name_token);
	type_def->type_algebra = ast::type_algebra_t::parse(ps, type_def->type_decl);
	return type_def;
}

type_algebra_t::ref type_algebra_t::parse(
		parse_state_t &ps,
		ast::type_decl_t::ref type_decl)
{
    indent_logger indent(type_decl->token.location, 8, string_format("parsing type algebra for %s",
                type_decl->token.text.c_str()));

	if (ps.token.is_ident(K(is))) {
		return data_type_t::parse(ps, type_decl, type_decl->type_variables);
	} else if (ps.token.is_ident(K(has))) {
		return type_product_t::parse(ps, type_decl, type_decl->type_variables, false /*native*/);
	} else if (ps.token.is_ident(K(link))) {
		return type_link_t::parse(ps, type_decl, type_decl->type_variables);
	} else if (ps.token.tk == tk_assign) {
		return type_alias_t::parse(ps, type_decl, type_decl->type_variables);
	} else if (ps.token.is_ident(K(struct))) {
		return type_product_t::parse(ps, type_decl, type_decl->type_variables, true /*native*/);
	} else {
		throw user_error(ps.token.location, 
				"type descriptions must begin with "
			   	c_id("is") ", " c_id("has") ", or " c_id("=") ". (Found %s)",
				ps.token.str().c_str());
	}
}

std::pair<token_t, types::type_args_t::ref> parse_ctor(
		parse_state_t &ps,
	   	identifiers_t type_variables_list)
{
	expect_token(tk_identifier);
	auto name = ps.token;
	if (!isupper(name.text[0])) {
		throw user_error(name.location, "constructor names must begin with an uppercase letter");
	}

	ps.advance();
	return {name, types::parse_data_ctor_type(ps, to_set(type_variables_list))};
}

data_type_t::ref data_type_t::parse(
		parse_state_t &ps,
		ast::type_decl_t::ref type_decl,
		identifiers_t type_variables_list)
{
	std::set<identifier_t> type_variables = to_set(type_variables_list);
	auto is_token = ps.token;
	chomp_ident(K(is));
	chomp_token(tk_lcurly);

	auto data_type = create<data_type_t>(type_decl->token);
	while (ps.token.tk == tk_identifier) {
		auto ctor_pair = parse_ctor(ps, type_variables_list);
		for (auto x : data_type->ctor_pairs) {
			if (x.first.text == ctor_pair.first.text) {
				auto error = user_error(ctor_pair.first.location, "duplicated data constructor name");
				error.add_info(x.first.location, "see initial declaration here");
				throw error;
			}
		}
		debug_above(8, log("parsed ctor %s for type " c_type("%s"), ctor_pair.first.str().c_str(), data_type->token.text.c_str()));
		data_type->ctor_pairs.push_back(ctor_pair);
	}

	chomp_token(tk_rcurly);

	return data_type;
}

type_product_t::ref type_product_t::parse(
		parse_state_t &ps,
		ast::type_decl_t::ref type_decl,
	   	identifiers_t type_variables,
		bool native)
{
	std::set<identifier_t> generics = to_identifier_set(type_variables);
	if (native) {
		expect_ident(K(struct));
	} else {
		expect_ident(K(has));
	}
	auto type = types::parse_product_type(ps, generics);
	return create<type_product_t>(type_decl->token, native, type, generics);
}

type_link_t::ref type_link_t::parse(
		parse_state_t &ps,
		ast::type_decl_t::ref type_decl,
		identifiers_t type_variables)
{
	std::set<identifier_t> generics = to_identifier_set(type_variables);
	chomp_ident(K(link));
	return create<type_link_t>(type_decl->token);

}

type_alias_t::ref type_alias_t::parse(
		parse_state_t &ps,
		ast::type_decl_t::ref type_decl,
	   	identifiers_t type_variables)
{
	chomp_token(tk_assign);

	std::set<identifier_t> generics = to_identifier_set(type_variables);
	types::type_t::ref type = types::parse_type(ps, generics);

	auto type_alias = ast::create<ast::type_alias_t>(type_decl->token);
	assert(type_alias->token.text != "");

	type_alias->parsed_type = parsed_type_t(type);
	type_alias->type_variables = generics;
	return type_alias;
}

dimension_t::ref dimension_t::parse(parse_state_t &ps, std::set<identifier_t> generics) {
	token_t primary_token;
	std::string name;
	if (ps.token.is_ident(K(var))) {
		ps.advance();
		expect_token(tk_identifier);
		primary_token = ps.token;
		name = primary_token.text;
		ps.advance();
	} else {
		throw user_error(ps.token.location, "not sure what's going on here");
	}

	types::type_t::ref type = types::parse_type(ps, generics);
	return ast::create<ast::dimension_t>(primary_token, name, type);
}
#endif 

module_t *parse_module(parse_state_t &ps, identifiers_t &module_deps) {
	debug_above(6, log("about to parse %s", ps.filename.c_str()));

	std::vector<decl_t *> decls;

	while (ps.token.is_ident(K(get))) {
		ps.advance();
		expect_token(tk_identifier);
		std::string module_name = ps.token.text;
		ps.advance();
		expect_token(tk_lcurly);
		while (ps.token.tk != tk_rcurly) {
			expect_token(tk_identifier);
			ps.add_term_map(ps.token.location, ps.token.text, module_name + "." + ps.token.text);
			ps.advance();
			if (ps.token.tk == tk_comma) {
				ps.advance();
			}
		}
	}

	while (ps.token.is_ident(K(fn))) {
		ps.advance();
		decls.push_back(new decl_t(
					identifier_t::from_token(ps.token_and_advance()),
					parse_lambda(ps)));
	}
	if (ps.token.tk != tk_none) {
		throw user_error(ps.token.location, "unknown stuff here");
	}
	return new module_t(ps.module_name, decls);
}

#if 0
void ff() {
	/* Get vars, functions or type defs */
	while (true) {
		if (ps.token.is_ident(K(link))) {
			auto link_statement = link_statement_parse(ps);
			if (auto linked_function = dyncast<link_function_statement_t>(link_statement)) {
				module->linked_functions.push_back(linked_function);
			} else if (auto linked_var = dyncast<link_var_statement_t>(link_statement)) {
				module->linked_vars.push_back(linked_var);
			}
		} else if (ps.token.is_ident(K(var)) || ps.token.is_ident(K(let))) {
			bool is_let = ps.token.is_ident(K(let));
			if (is_let) {
				throw user_error(ps.token.location, "let variables are not yet supported at the module level");
			} else {
				ps.advance();
				auto var = var_decl_t::parse(ps, is_let, false /*allow_tuple_destructuring*/);
				auto var_decl = dyncast<var_decl_t>(var);
				assert(var_decl != nullptr);
				module->var_decls.push_back(var_decl);
			}
		} else if (ps.token.tk == tk_lsquare || ps.token.is_ident(K(fn))) {
			/* function definitions */
			auto function = function_defn_t::parse(ps, false /*within_expression*/);
			if (function->token.text == "main") {
				bool have_linked_main = false;
				for (auto linked_module : module->linked_modules) {
					if (linked_module->token.text == "main") {
						have_linked_main = true;
						break;
					}
				}
				if (!have_linked_main && getenv("NO_STD_MAIN") == nullptr) {
					std::shared_ptr<link_module_statement_t> linked_module = create<link_module_statement_t>(ps.token);
					linked_module->link_as_name = token_t(
							function->decl->token.location,
							tk_identifier,
							types::gensym(INTERNAL_LOC())->get_name());
					linked_module->extern_module = create<ast::module_decl_t>(token_t(
								function->decl->token.location,
								tk_identifier,
								"main"));
					linked_module->extern_module->name = linked_module->extern_module->token;
					module->linked_modules.push_back(linked_module);
				}
			}
			module->functions.push_back(std::move(function));
		} else if (ps.token.is_ident(K(type))) {
			/* type definitions */
			auto type_def = type_def_t::parse(ps);
			module->type_defs.push_back(type_def);
			if (module->global) {
				auto id = iid(type_def->token);
				ps.type_macros.insert({type_def->token.text, type_id(id)});
				ps.global_type_macros.insert({type_def->token.text, type_id(id)});
			}
		} else {
			break;
		}
	}

	if (ps.token.is_ident(K(link))) {
		throw user_error(ps.token.location, C_MODULE "link" C_RESET " directives must come before types, variables, and functions");
	} else if (ps.token.tk != tk_none) {
		throw user_error(ps.token.location, "unexpected '" c_id("%s") "' at top-level module scope (%s)",
				ps.token.text.c_str(), tkstr(ps.token.tk));
	}

	return module;
}
#endif
