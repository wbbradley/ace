#include <stdlib.h>
#include <string>
#include "ast.h"
#include "token.h"
#include "logger_decls.h"
#include <csignal>
#include "parse_state.h"
#include "parsed_id.h"

using namespace ast;


#define eat_token_or_return(fail_code) \
	do { \
		debug_above(4, log(log_info, "eating a %s", tkstr(ps.token.tk))); \
		ps.advance(); \
	} while (0)

#define eat_token() eat_token_or_return(nullptr)

#define expect_token_or_return(_tk, fail_code) \
	do { \
		if (ps.token.tk != _tk) { \
			ps.error("expected %s, got %s [at %s:%d]", tkstr(_tk), tkstr(ps.token.tk), \
					__FILE__, __LINE__); \
			dbg(); \
			return fail_code; \
		} \
	} while (0)

#define expect_token(_tk) expect_token_or_return(_tk, nullptr)

#define chomp_token_or_return(_tk, fail_code) \
	do { \
		expect_token_or_return(_tk, fail_code); \
		eat_token_or_return(fail_code); \
	} while (0)
#define chomp_token(_tk) chomp_token_or_return(_tk, nullptr)

ptr<var_decl> var_decl::parse(parse_state_t &ps) {
	expect_token(tk_identifier);

	auto var_decl = create<ast::var_decl>(ps.token);
	eat_token();

	if (ps.token.tk != tk_assign) {
		var_decl->type_ref = type_ref::parse(ps);
	}

	if (ps.token.tk == tk_assign) {
		eat_token();
		var_decl->initializer = expression::parse(ps);
	}

	return var_decl;
}

ptr<var_decl> var_decl::parse_param(parse_state_t &ps) {
	expect_token(tk_identifier);

	auto var_decl = create<ast::var_decl>(ps.token);
	eat_token();

	if (ps.token.tk == tk_assign) {
		ps.error("default values for function arguments are not a thing");
		return nullptr;
	} else if (ps.token.tk == tk_comma || ps.token.tk == tk_rparen) {
		/* ok, assume it's just "any" later */
		var_decl->type_ref = nullptr;
	} else {
		var_decl->type_ref = type_ref::parse(ps);
	}

	return var_decl;
}

ptr<return_statement> return_statement::parse(parse_state_t &ps) {
	auto return_statement = create<ast::return_statement>(ps.token);
	chomp_token(tk_return);
	if (!ps.line_broke() && ps.token.tk != tk_outdent) {
		return_statement->expr = expression::parse(ps);
		if (!return_statement->expr) {
			assert(!ps.status);
			return nullptr;
		}
	}
	return return_statement;
}

ptr<statement> link_statement_parse(parse_state_t &ps) {
	expect_token(tk_link);
	auto link_token = ps.token;
	ps.advance();

	if (ps.token.tk == tk_def) {
		auto link_function_statement = create<ast::link_function_statement>(link_token);
		auto function_decl = function_decl::parse(ps);
		if (function_decl) {
			link_function_statement->link_as_name = function_decl->token;
			link_function_statement->extern_function.swap(function_decl);
		} else {
			assert(!ps.status);
		}
		return link_function_statement;
	} else {
		auto link_statement = create<link_module_statement>(link_token);

		if (ps.token.tk == tk_identifier) {
			link_statement->link_as_name = ps.token;
			ps.advance();
			chomp_token(tk_to);
		}

		if (ps.token.tk == tk_module) {
			auto module_decl = module_decl::parse(ps);
			if (module_decl) {
				if (link_statement->link_as_name.tk == tk_nil) {
					link_statement->link_as_name = module_decl->get_name();
				}
				if (link_statement->link_as_name.tk != tk_identifier) {
					ps.error("expected an identifier for link module name (either implicit or explicit)");
				}
				link_statement->extern_module.swap(module_decl);
			} else {
				assert(!ps.status);
			}
		} else {
			ps.error("link must be followed by function declaration or module import");
		}

		return link_statement;
	}
}

ptr<statement> statement::parse(parse_state_t &ps) {
	assert(ps.token.tk != tk_outdent);

	if (ps.token.tk == tk_var) {
		eat_token();
		return var_decl::parse(ps);
	} else if (ps.token.tk == tk_if) {
		return if_block::parse(ps);
	} else if (ps.token.tk == tk_while) {
		return while_block::parse(ps);
	} else if (ps.token.tk == tk_return) {
		return return_statement::parse(ps);
	} else if (ps.token.tk == tk_type) {
		return type_def::parse(ps);
	} else if (ps.token.tk == tk_link) {
		return link_statement_parse(ps);
	} else if (ps.token.tk == tk_pass) {
		auto pass_flow = create<ast::pass_flow>(ps.token);
		eat_token();
		return std::move(pass_flow);
	} else if (ps.token.tk == tk_continue) {
		auto continue_flow = create<ast::continue_flow>(ps.token);
		eat_token();
		return std::move(continue_flow);
	} else if (ps.token.tk == tk_def) {
		return function_defn::parse(ps);
	} else if (ps.token.tk == tk_break) {
		auto break_flow = create<ast::break_flow>(ps.token);
		eat_token();
		return std::move(break_flow);
	} else {
		return assignment::parse(ps);
	}
}

ptr<expression> reference_expr::parse(parse_state_t &ps) {
	if (ps.token.tk == tk_identifier) {
		auto reference_expr = create<ast::reference_expr>(ps.token);
		ps.advance();
		return std::move(reference_expr);
	} else {
		ps.error("expected an identifier");
		return nullptr;
	}
}

ptr<expression> base_expr::parse(parse_state_t &ps) {
	if (ps.token.tk == tk_lparen) {
		auto expr = tuple_expr::parse(ps);
		return expr;
	} else if (ps.token.tk == tk_identifier) {
		return reference_expr::parse(ps);
	} else {
		return literal_expr::parse(ps);
	}
}

ptr<expression> array_literal_expr::parse(parse_state_t &ps) {
	chomp_token(tk_lsquare);
	auto array = create<array_literal_expr>(ps.token);
	auto &items = array->items;

	int i = 0;
	while (ps.token.tk != tk_rsquare && ps.token.tk != tk_nil) {
		++i;
		auto item = expression::parse(ps);
		if (item) {
			items.push_back(item);
		} else {
			assert(!ps.status);
		}
		if (ps.token.tk == tk_comma) {
			ps.advance();
		} else if (ps.token.tk != tk_rsquare) {
			ps.error("found something that does not make sense in an array literal");
			break;
		}
	}
	chomp_token(tk_rsquare);
	return array;
}

ptr<expression> literal_expr::parse(parse_state_t &ps) {
	switch (ps.token.tk) {
	case tk_null:
	case tk_integer:
	case tk_string:
	case tk_char:
	case tk_float:
	case tk_true:
	case tk_false:
		{
			auto literal_expr = create<ast::literal_expr>(ps.token);
			ps.advance();
			return std::move(literal_expr);
		}
	case tk_lsquare:
		return array_literal_expr::parse(ps);
	case tk_def:
		return function_defn::parse(ps);
	case tk_indent:
		ps.error("unexpected indent");
		return nullptr;

	default:
		ps.error("out of place token '" c_id("%s") "' (%s)",
			   	ps.token.text.c_str(), tkstr(ps.token.tk));
		return nullptr;
	}
}

namespace ast {
	namespace postfix_expr {
		ptr<expression> parse(parse_state_t &ps) {
			ptr<expression> expr = base_expr::parse(ps);
			if (!expr) {
				assert(!ps.status);
				return nullptr;
			}

			while (!ps.line_broke() && (ps.token.tk == tk_lsquare || ps.token.tk == tk_lparen || ps.token.tk == tk_dot)) {
				if (ps.token.tk == tk_lparen) {
					/* function call */
					auto callsite = create<callsite_expr>(ps.token);
					auto params = param_list::parse(ps);
					if (params) {
						callsite->params.swap(params);
						callsite->function_expr.swap(expr);
						assert(expr == nullptr);
						expr = ptr<expression>(std::move(callsite));
					} else {
						assert(!ps.status);
					}
				}
				if (ps.token.tk == tk_dot) {
					auto dot_expr = create<ast::dot_expr>(ps.token);
					eat_token();
					expect_token(tk_identifier);
					dot_expr->rhs = ps.token;
					ps.advance();
					dot_expr->lhs.swap(expr);
					assert(expr == nullptr);
					expr = ptr<expression>(std::move(dot_expr));
				}
				if (ps.token.tk == tk_lsquare) {
					eat_token();
					auto array_index_expr = create<ast::array_index_expr>(ps.token);

					auto index = expression::parse(ps);
					if (index) {
						array_index_expr->index.swap(index);
						array_index_expr->lhs.swap(expr);
						assert(expr == nullptr);
						expr = ptr<expression>(std::move(array_index_expr));
					} else {
						assert(!ps.status);
						return nullptr;
					}
					chomp_token(tk_rsquare);
				}
			}

			return expr;
		}
	}
}

ptr<expression> prefix_expr::parse(parse_state_t &ps) {
	ptr<prefix_expr> prefix_expr;
	if (ps.token.tk == tk_not
			|| ps.token.tk == tk_minus
			|| ps.token.tk == tk_plus)
	{
		prefix_expr = create<ast::prefix_expr>(ps.token);
		eat_token();
	}

	ptr<expression> rhs;
	if (ps.token.tk == tk_not
			|| ps.token.tk == tk_minus
			|| ps.token.tk == tk_plus) {
		/* recurse to find more prefix expressions */
		rhs = prefix_expr::parse(ps);
	} else {
		/* ok, we're done with prefix operators */
		rhs = postfix_expr::parse(ps);
	}

	if (rhs) {
		if (prefix_expr) {
			prefix_expr->rhs = std::move(rhs);
			return std::move(prefix_expr);
		} else {
			return rhs;
		}
	} else {
		assert(!ps.status);
		return nullptr;
	}
}

ptr<expression> times_expr::parse(parse_state_t &ps) {
	auto expr = prefix_expr::parse(ps);
	if (!expr) {
		assert(!ps.status);
		return nullptr;
	}

	while (!ps.line_broke() && (ps.token.tk == tk_times
			   	|| ps.token.tk == tk_divide_by
			   	|| ps.token.tk == tk_mod)) {
		auto times_expr = create<ast::times_expr>(ps.token);

		eat_token();

		auto rhs = prefix_expr::parse(ps);
		if (rhs) {
			times_expr->lhs = std::move(expr);
			times_expr->rhs = std::move(rhs);
			expr = std::move(times_expr);
		} else {
			ps.error("unable to parse right hand side of times_expr");
			return nullptr;
		}
	}

	return expr;
}

ptr<expression> plus_expr::parse(parse_state_t &ps) {
	auto expr = times_expr::parse(ps);
	if (!expr) {
		assert(!ps.status);
		return nullptr;
	}

	while (!ps.line_broke() && (ps.token.tk == tk_plus || ps.token.tk == tk_minus)) {
		auto plus_expr = create<ast::plus_expr>(ps.token);

		eat_token();

		auto rhs = times_expr::parse(ps);
		if (rhs) {
			plus_expr->lhs = std::move(expr);
			plus_expr->rhs = std::move(rhs);
			expr = std::move(plus_expr);
		} else {
			ps.error("unable to parse right hand side of plus_expr");
			return nullptr;
		}
	}

	return expr;
}

ptr<expression> ineq_expr::parse(parse_state_t &ps) {
	auto lhs = plus_expr::parse(ps);
	if (lhs) {
		if (ps.line_broke()
				|| !(ps.token.tk == tk_gt
					|| ps.token.tk == tk_gte
					|| ps.token.tk == tk_lt
					|| ps.token.tk == tk_lte)) {
			/* there is no rhs */
			return lhs;
		}

		auto ineq_expr = create<ast::ineq_expr>(ps.token);

		eat_token();

		auto rhs = plus_expr::parse(ps);
		if (rhs) {
			ineq_expr->lhs = std::move(lhs);
			ineq_expr->rhs = std::move(rhs);
			return std::move(ineq_expr);
		} else {
			ps.error("unable to parse right hand side of ineq_expr");
			return nullptr;
		}
	} else {
		assert(!ps.status);
		return nullptr;
	}
}

ptr<expression> eq_expr::parse(parse_state_t &ps) {
	auto lhs = ineq_expr::parse(ps);
	if (lhs) {
		bool not_in = false;
		if (ps.token.tk == tk_not) {
			eat_token();
			expect_token(tk_in);
			not_in = true;
		}

		if (ps.line_broke() ||
				!(ps.token.tk == tk_in
					|| ps.token.tk == tk_equal
					|| ps.token.tk == tk_inequal)) {
			/* there is no rhs */
			return lhs;
		}

		auto eq_expr = create<ast::eq_expr>(ps.token);
		eq_expr->not_in = not_in;

		eat_token();

		auto rhs = ineq_expr::parse(ps);
		if (rhs) {
			eq_expr->lhs = std::move(lhs);
			eq_expr->rhs = std::move(rhs);
			return std::move(eq_expr);
		} else {
			ps.error("unable to parse right hand side of eq_expr");
			return nullptr;
		}
	} else {
		assert(!ps.status);
		return nullptr;
	}
}

ptr<expression> and_expr::parse(parse_state_t &ps) {
	auto expr = eq_expr::parse(ps);
	if (!expr) {
		assert(!ps.status);
		return nullptr;
	}

	while (!ps.line_broke() && (ps.token.tk == tk_and)) {
		auto and_expr = create<ast::and_expr>(ps.token);

		eat_token();

		auto rhs = eq_expr::parse(ps);
		if (rhs) {
			and_expr->lhs = std::move(expr);
			and_expr->rhs = std::move(rhs);
			expr = std::move(and_expr);
		} else {
			ps.error("unable to parse right hand side of and_expr");
			return nullptr;
		}
	}

	return expr;
}

ptr<expression> tuple_expr::parse(parse_state_t &ps) {
	auto start_token = ps.token;
	chomp_token(tk_lparen);
	auto expr = or_expr::parse(ps);
	if (ps.token.tk != tk_comma) {
		chomp_token(tk_rparen);
		return expr;
	} else {
		ps.advance();

		/* we've got a tuple */
		auto tuple_expr = create<ast::tuple_expr>(start_token);

		/* add the first value */
		tuple_expr->values.push_back(expr);

		/* now let's find the rest of the values */
		while (ps.token.tk != tk_rparen) {
			expr = or_expr::parse(ps);
			if (expr) {
				tuple_expr->values.push_back(expr);
				if (ps.token.tk == tk_comma) {
					eat_token();
				} else if (ps.token.tk != tk_rparen) {
					ps.error(
						"unexpected token " c_id("%s") " in tuple. expected comma or right-paren",
						ps.token.text.c_str());
					return nullptr;
				}
				// continue and read the next parameter
			} else {
				assert(!ps.status);
				return nullptr;
			}
		}
		chomp_token(tk_rparen);
		return tuple_expr;
	}
}

ptr<expression> or_expr::parse(parse_state_t &ps) {
	auto expr = and_expr::parse(ps);
	if (!expr) {
		assert(!ps.status);
		return nullptr;
	}

	while (!ps.line_broke() && (ps.token.tk == tk_or)) {
		auto or_expr = create<ast::or_expr>(ps.token);

		eat_token();

		auto rhs = and_expr::parse(ps);
		if (rhs) {
			or_expr->lhs = std::move(expr);
			or_expr->rhs = std::move(rhs);
			expr = std::move(or_expr);
		} else {
			ps.error("unable to parse right hand side of or_expr");
			return nullptr;
		}
	}

	return expr;
}

ptr<expression> expression::parse(parse_state_t &ps) {
	return or_expr::parse(ps);
}

ptr<statement> assignment::parse(parse_state_t &ps) {
	auto lhs = expression::parse(ps);
	if (lhs) {

#define handle_assign(tk_, type) \
		if (!ps.line_broke() && ps.token.tk == tk_) { \
			auto assignment = create<type>(ps.token); \
			chomp_token(tk_); \
			auto rhs = expression::parse(ps); \
			if (rhs) { \
				assignment->lhs = std::move(lhs); \
				assignment->rhs = std::move(rhs); \
				return std::move(assignment); \
			} else { \
				assert(!ps.status); \
				return nullptr; \
			} \
		}

		handle_assign(tk_assign, ast::assignment);
		handle_assign(tk_plus_eq, ast::plus_assignment);
		handle_assign(tk_minus_eq, ast::minus_assignment);
		handle_assign(tk_divide_by_eq, ast::divide_assignment);
		handle_assign(tk_times_eq, ast::times_assignment);
		handle_assign(tk_mod_eq, ast::mod_assignment);

		if (!ps.line_broke() && ps.token.tk == tk_becomes) {
			if (lhs->sk == sk_reference_expr) {
				chomp_token(tk_becomes);
				auto var_decl = create<ast::var_decl>(lhs->token);
				auto initializer = or_expr::parse(ps);
				if (initializer) {
					var_decl->initializer.swap(initializer);
					return std::move(var_decl);
				} else {
					assert(!ps.status);
					return nullptr;
				}
			} else {
				ps.error(":= may only come after a reference_expr");
				return nullptr;
			}
		} else {
			return lhs;
		}
		return lhs;
	} else {
		assert(!ps.status);
	}

	return nullptr;
}

ptr<param_list_decl> param_list_decl::parse(parse_state_t &ps) {
	/* reset the argument index */
	ps.argument_index = 0;

	auto param_list_decl = create<ast::param_list_decl>(ps.token);
	while (ps.token.tk != tk_rparen) {
		param_list_decl->params.push_back(var_decl::parse_param(ps));
		if (ps.token.tk == tk_comma) {
			eat_token();
		} else if (ps.token.tk != tk_rparen) {
			ps.error("unexpected token in param_list_decl");
			return nullptr;
		}
	}
	return param_list_decl;
}

ptr<param_list> param_list::parse(parse_state_t &ps) {
	auto param_list = create<ast::param_list>(ps.token);
	chomp_token(tk_lparen);
	int i = 0;
	while (ps.token.tk != tk_rparen) {
		++i;
		auto expr = expression::parse(ps);
		if (expr) {
			param_list->expressions.push_back(std::move(expr));
			if (ps.token.tk == tk_comma) {
				eat_token();
			} else if (ps.token.tk != tk_rparen) {
				ps.error("unexpected token " c_id("%s") " in parameter list", ps.token.text.c_str());
				return nullptr;
			}
			// continue and read the next parameter
		} else {
			assert(!ps.status);
			return nullptr;
		}
	}
	chomp_token(tk_rparen);

	return param_list;
}

ptr<block> block::parse(parse_state_t &ps) {
	auto block = create<ast::block>(ps.token);
	chomp_token(tk_indent);
	if (ps.token.tk == tk_outdent) {
		ps.error("empty blocks are not allowed, sorry. use pass.");
		return nullptr;
	}

	while (!!ps.status && ps.token.tk != tk_outdent) {
		assert(ps.token.tk != tk_nil);
		while (ps.token.tk == tk_semicolon) {
			ps.advance();
		}
		if (!ps.line_broke()
				&& !(ps.prior_token.tk == tk_indent
					|| ps.prior_token.tk == tk_outdent)) {
			ps.error("statements must be separated by a newline (or a semicolon)");
		}
		auto statement = statement::parse(ps);
		if (statement) {
			block->statements.push_back(std::move(statement));
		} else {
			assert(!ps.status);
			return nullptr;
		}
	}

	expect_token(tk_outdent);
	ps.advance();
	return block;
}

ptr<if_block> if_block::parse(parse_state_t &ps) {
	auto if_block = create<ast::if_block>(ps.token);
	if (ps.token.tk == tk_if || ps.token.tk == tk_elif) {
		ps.advance();
	} else {
		ps.error("expected if or elif");
		return nullptr;
	}

	auto condition = expression::parse(ps);
	if (condition) {
		if_block->condition.swap(condition);
		auto block = block::parse(ps);
		if (block) {
			if_block->block.swap(block);

			if (ps.prior_token.tk == tk_outdent) {
				/* check the successive instructions for elif or else */
				if (ps.token.tk == tk_elif) {
					if_block->else_ = if_block::parse(ps);
				} else if (ps.token.tk == tk_else) {
					ps.advance();
					if_block->else_ = block::parse(ps);
				}
			}

			return if_block;
		} else {
			assert(!ps.status);
			return nullptr;
		}
	} else {
		assert(!ps.status);
		return nullptr;
	}
}

ptr<while_block> while_block::parse(parse_state_t &ps) {
	auto while_block = create<ast::while_block>(ps.token);
	chomp_token(tk_while);
	auto condition = expression::parse(ps);
	if (condition) {
		while_block->condition.swap(condition);
		auto block = block::parse(ps);
		if (block) {
			while_block->block.swap(block);
			return while_block;
		} else {
			assert(!ps.status);
			return nullptr;
		}
	} else {
		assert(!ps.status);
		return nullptr;
	}
}

ptr<function_decl> function_decl::parse(parse_state_t &ps) {
	chomp_token(tk_def);

	auto function_decl = create<ast::function_decl>(ps.token);

	chomp_token(tk_identifier);
	chomp_token(tk_lparen);

	function_decl->param_list_decl = param_list_decl::parse(ps);

	chomp_token(tk_rparen);
	if (ps.token.tk == tk_identifier || ps.token.tk == tk_any) {
		function_decl->return_type_ref = type_ref::parse(ps);
	}

	return function_decl;
}

ptr<function_defn> function_defn::parse(parse_state_t &ps) {
	auto function_decl = function_decl::parse(ps);

	if (function_decl) {
		auto block = block::parse(ps);
		if (block) {
			auto function_defn = create<ast::function_defn>(function_decl->token);
			function_defn->decl.swap(function_decl);
			function_defn->block.swap(block);
			return function_defn;
		} else {
			assert(!ps.status);
			return nullptr;
		}
	} else {
		assert(!ps.status);
		return nullptr;
	}
}

ptr<module_decl> module_decl::parse(parse_state_t &ps) {
	auto module_decl = create<ast::module_decl>(ps.token);

	chomp_token(tk_module);

	expect_token(tk_identifier);
	module_decl->name = ps.token;
	eat_token();

	if (ps.token.tk == tk_version) {
		auto semver = semver::parse(ps);
		if (semver) {
			module_decl->semver.swap(semver);
		} else {
			/* ok for now */
		}
	}
	return module_decl;
}

ptr<semver> semver::parse(parse_state_t &ps) {
	if (ps.token.tk == tk_version) {
		auto semver = create<ast::semver>(ps.token);
		eat_token();
		return semver;
	} else {
		return nullptr;
	}

}

void parse_type_decl(parse_state_t &ps, atom &name, atom::many &type_variables)
{
	return;
}

types::identifier::ref make_parsed_id(const zion_token_t &token) {
	return make_ptr<parsed_id_t>(token);
}

types::term::ref parse_term(parse_state_t &ps, int depth=0) {
	if (ps.token.tk == tk_any) {
		/* parse generic refs */
		ps.advance();
		if (ps.token.tk == tk_identifier) {
			/* named generic */
			auto term = term_generic(make_parsed_id(ps.token));
			ps.advance();
			return term;
		} else {
			/* no named generic */
			// TODO: include the location information
			return types::term_generic();
		}
	} else {
		/* ensure that we are looking at an identifier */
		expect_token_or_return(tk_identifier, types::term_unreachable());

		/* stash the identifier */
		types::term::ref cur_term = term_id(make_parsed_id(ps.token));

		/* move on */
		ps.advance();

		if (ps.token.tk == tk_lcurly) {
			/* skip the curly */
			ps.advance();

			/* loop over the type arguments */
			while (!!ps.status && ps.token.tk != tk_rcurly) {
				if (ps.token.tk == tk_identifier || ps.token.tk == tk_any) {
					/* we got an argument, recursively parse */
					auto next_term = parse_term(ps, depth + 1);
					if (!!ps.status) {
						cur_term = term_apply(cur_term, next_term);

						if (ps.token.tk == tk_rcurly) {
							/* move on */
							break;
						} else if (ps.token.tk == tk_comma) {
							/* if we get a comma, move past it */
							ps.advance();
						} else {
							ps.error("expected ('}' or ','), got %s", tkstr(ps.token.tk));
						}
					}
				} else {
					ps.error("expected an identifier in the type declaration, found " c_id("%s"),
							tkstr(ps.token.tk));
				}
			}

			if (ps.token.tk == tk_rcurly) {
				ps.advance();
			} else {
				assert(!ps.status);
			}
		}

		return cur_term;
	}
}

types::term::ref parse_type_expr(std::string input) {
	status_t status;
	std::istringstream iss(input);
	zion_lexer_t lexer("", iss);
	parse_state_t ps(status, "", lexer, nullptr);
	types::term::ref term = null_impl(); // parse_term(ps, psc_type_ref);
	if (!!status) {
		return term;
	} else {
		panic("bad term");
		return null_impl();
	}
}

types::term::ref operator "" _ty(const char *value, size_t) {
	return parse_type_expr(value);
}

type_decl::ref type_decl::parse(parse_state_t &ps) {
	atom name;
	atom::many type_variables;
	parse_type_decl(ps, name, type_variables);

	if (!!ps.status) {
		return create<ast::type_decl>(ps.token, name, type_variables);
	} else {
		return nullptr;
	}
}

ptr<type_def> type_def::parse(parse_state_t &ps) {
	chomp_token(tk_type);
	auto type_def = create<ast::type_def>(ps.token);
	type_def->type_decl = type_decl::parse(ps);
	if (!!ps.status) {
		type_def->type_algebra = ast::type_algebra::parse(ps, type_def->type_decl);
		if (!!ps.status) {
			return type_def;
		}
	}

	assert(!ps.status);
	return nullptr;
}

type_algebra::ref type_algebra::parse(
		parse_state_t &ps,
		ast::type_decl::ref type_decl)
{
	switch (ps.token.tk) {
	case tk_is:
		return type_sum::parse(ps, type_decl->type_variables);
	case tk_has:
		return type_product::parse(ps, type_decl->type_variables);
	case tk_matches:
		return type_alias::parse(ps, type_decl->type_variables);
	default:
		ps.error("type descriptions must begin with " c_id("is") ", " c_id("has") ", or " c_id("matches") ".");
		return nullptr;
	}
}

type_sum::ref type_sum::parse(
		parse_state_t &ps,
		atom::many type_variables_list)
{
	atom::set type_variables;
	std::for_each(
		type_variables_list.begin(),
	   	type_variables_list.end(),
		[&] (atom name) {
			type_variables.insert(name);
		});
	auto is_token = ps.token;
	chomp_token(tk_is);
	bool expect_outdent = false;
	if (ps.token.tk == tk_indent) {
		/* take note of whether the user has indented or not */
		expect_outdent = true;
		ps.advance();
	}

	if (ps.token.tk != tk_identifier) {
		ps.error("sum types must begin with an identifier. found " c_error("%s"),
				ps.token.text.c_str());
	}

	std::vector<data_ctor::ref> data_ctors;
	while (!!ps.status) {
		data_ctors.push_back(data_ctor::parse(ps, type_variables));

		if (ps.token.tk != tk_or) {
			break;
		} else {
			chomp_token(tk_or);
		}
	}

	if (!!ps.status) {
		if (expect_outdent) {
			chomp_token(tk_outdent);
		}

		return create<type_sum>(is_token, data_ctors);
	} else {
		return nullptr;
	}
}

type_product::ref type_product::parse(
		parse_state_t &ps,
	   	atom::many type_variables)
{
	auto has_token = ps.token;
	chomp_token(tk_has);
	chomp_token(tk_indent);
	std::vector<dimension::ref> dimensions;
	while (!!ps.status && ps.token.tk != tk_outdent) {
		if (!ps.line_broke() && ps.prior_token.tk != tk_indent) {
			ps.error("product type dimensions must be separated by a newline");
		}
		dimensions.push_back(dimension::parse(ps));
	}
	chomp_token(tk_outdent);
	if (!!ps.status) {
		return create<type_product>(has_token, dimensions,
			   	to_set(type_variables));
	} else {
		return nullptr;
	}
}

type_alias::ref type_alias::parse(parse_state_t &ps, atom::many type_variables) {
	chomp_token(tk_matches);

	return ast::create<type_alias>(ps.token, type_ref::parse(ps),
			to_set(type_variables));
}

type_ref::ref type_ref::parse(parse_state_t &ps) {
	if (ps.token.tk == tk_lsquare) {
		return type_ref_list::parse(ps);
	} else if (ps.token.tk == tk_lcurly) {
		return type_ref_tuple::parse(ps);
	} else if (ps.token.tk == tk_identifier) {
		return type_ref_named::parse(ps);
	} else if (ps.token.tk == tk_any) {
		return type_ref_generic::parse(ps);
	} else {
		ps.error("expected an identifier when parsing a type_ref");
		return nullptr;
	}
}

type_ref::ref type_ref_named::parse(parse_state_t &ps) {
	assert(ps.token.tk != tk_any);
	return create<ast::type_ref_named>(ps.token, parse_term(ps));
}

type_ref::ref type_ref_list::parse(parse_state_t &ps) {
	chomp_token(tk_lsquare);
	type_ref::ref type_ref_list = create<ast::type_ref_list>(ps.token,
		   	type_ref::parse(ps));
	chomp_token(tk_rsquare);
	return type_ref_list;
}

type_ref::ref type_ref_tuple::parse(parse_state_t &ps) {
	zion_token_t tuple_token = ps.token;
	chomp_token(tk_lcurly);

	std::vector<type_ref::ref> type_refs;
   	while (ps.token.tk != tk_rcurly) {
		/* parse the nested type_ref */
		type_ref::ref type_ref = ast::type_ref::parse(ps);

		/* add the parsed type_ref to our tuple list */
		type_refs.push_back(type_ref);

		if (ps.token.tk == tk_comma) {
			ps.advance();
		} else if (ps.token.tk == tk_rparen) {
			break;
		}
	}

	chomp_token(tk_rcurly);

	return ast::create<type_ref_tuple>(tuple_token, type_refs);
}

type_ref::ref type_ref_generic::parse(parse_state_t &ps) {
	assert(ps.token.tk == tk_any);
	return create<ast::type_ref_generic>(ps.token, parse_term(ps));
}

dimension::ref dimension::parse(parse_state_t &ps) {
	zion_token_t primary_token;
	atom name;
	if (ps.token.tk == tk_var) {
		ps.advance();
		expect_token(tk_identifier);
		primary_token = ps.token;
		name = primary_token.text;
		ps.advance();
	} else {
		expect_token(tk_identifier);
		primary_token = ps.token;
	}

	return create<ast::dimension>(primary_token, name, ast::type_ref::parse(ps));
}

data_ctor::ref data_ctor::parse(
		parse_state_t &ps,
		atom::set type_variables)
{
	expect_token(tk_identifier);
	zion_token_t name_token = ps.token;
	ps.advance();

	std::vector<type_ref::ref> type_ref_params;

	if (ps.token.tk == tk_lparen) {
		ps.advance();
		while (!!ps.status) {
			type_ref::ref type_ref = ast::type_ref::parse(ps);
			if (!!ps.status) {
				type_ref_params.push_back(type_ref);
			}
			if (ps.token.tk != tk_comma) {
				break;
			}
			ps.advance();
		}
		if (!!ps.status) {
			chomp_token(tk_rparen);
		}
	}

	if (!!ps.status) {
		return ast::create<data_ctor>(name_token, type_variables,
				type_ref_params);
	} else {
		return nullptr;
	}
}

ptr<module> module::parse(parse_state_t &ps) {
	auto module_decl = module_decl::parse(ps);

	if (module_decl) {
		auto module = create<ast::module>(ps.token, ps.filename);
		module->decl.swap(module_decl);

		// Get links
		while (ps.token.tk == tk_link) {
			auto link_statement = link_statement_parse(ps);
			if (auto linked_module = dyncast<link_module_statement>(link_statement)) {
				module->linked_modules.push_back(linked_module);
			} else if (auto linked_function = dyncast<link_function_statement>(link_statement)) {
				module->linked_functions.push_back(linked_function);
			}
		}
		
		// Get functions or type defs
		while (true) {
			if (ps.token.tk == tk_def) {
				auto function = function_defn::parse(ps);
				if (function) {
					module->functions.push_back(std::move(function));
				} else {
					assert(!ps.status);
				}
			} else if (ps.token.tk == tk_type) {
				auto type_def = type_def::parse(ps);
				if (type_def) {
					module->type_defs.push_back(std::move(type_def));
				} else {
					assert(!ps.status);
				}
			} else {
				break;
			}
		}

		if (ps.token.tk != tk_nil) {
			if (!!ps.status) {
				ps.error("unexpected '" c_error("%s") "' at top-level module scope",
						tkstr(ps.token.tk));
			}
		}
		return module;
	} else {
		assert(!ps.status);
	}
	return nullptr;
}
