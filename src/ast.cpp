#include "parens.h"
#include "ast.h"
#include "code_id.h"

#define MATHY_SYMBOLS "!@#$%^&*()+-_=><.,/|[]`~\\"

std::ostream &operator <<(std::ostream &os, bitter::program_t *program) {
	os << "program";
	const char *delim = "\n";
	for (auto decl : program->decls) {
		os << delim << decl;
	}
	return os << std::endl;
}

std::ostream &operator <<(std::ostream &os, bitter::decl_t *decl) {
	os << decl->var->get_name() << " = ";
	return decl->value->render(os, 0);
}

namespace bitter {
	location_t var_t::get_location() {
		return var->get_location();
	}
	std::ostream &var_t::render(std::ostream &os, int parent_precedence) {
		return os << var->get_name();
	}
	location_t as_t::get_location() {
		return type->get_location();
	}
	std::ostream &as_t::render(std::ostream &os, int parent_precedence) {
		os << "(";
		expr->render(os, 10);
		os << C_TYPE " as " C_RESET;
		type->emit(os, {}, 0);
		os << ")";
		return os;
	}
	location_t application_t::get_location() {
		return a->get_location();
	}
	std::ostream &application_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 5;
		if (auto inner_app = dcast<application_t *>(a)) {
			if (auto oper = dcast<var_t *>(inner_app->a)) {
				if (strspn(oper->var->get_name().c_str(), MATHY_SYMBOLS) == oper->var->get_name().size()) {
					os << "(";
					inner_app->b->render(os, precedence + 1);
					os << " ";
					oper->render(os, precedence);
					os << " ";
					b->render(os, precedence + 1);
					os << ")";
					return os;
				}
			}
		}

		parens_t parens(os, parent_precedence, precedence);
		a->render(os, precedence);
		os << " ";
		b->render(os, precedence + 1);
		return os;
	}
	location_t return_statement_t::get_location() {
		return value->get_location();
	}
	std::ostream &return_statement_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 4;
		parens_t parens(os, parent_precedence, precedence);
		os << C_CONTROL "return " C_RESET;
		value->render(os, 0);
		return os;
	}
	location_t match_t::get_location() {
		return scrutinee->get_location();
	}
	std::ostream &match_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 4;
		parens_t parens(os, parent_precedence, precedence);
		os << "match ";
		scrutinee->render(os, 6);
		for (auto pattern_block : pattern_blocks) {
			os << " ";
			pattern_block->render(os);
		}
		return os;
	}
	location_t while_t::get_location() {
		return condition->get_location();
	}
	std::ostream &while_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 3;
		parens_t parens(os, parent_precedence, precedence);
		os << C_CONTROL "while " C_RESET;
		condition->render(os, 6);
		os << " ";
		block->render(os, precedence);
		return os;
	}
	location_t block_t::get_location() {
		assert(statements.size() != 0);
		return statements[0]->get_location();
	}
	std::ostream &block_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 0;
		parens_t parens(os, parent_precedence, precedence);
		const char *delim = "";
		for (auto statement: statements) {
			os << delim;
			statement->render(os, precedence);
			delim = "; ";
		}
		return os;
	}
	location_t lambda_t::get_location() {
		return var->get_location();
	}
	std::ostream &lambda_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 7;
		os << "(λ" << var->get_name() << ".";
		body->render(os, 0);
		return os << ")";
	}
	location_t let_t::get_location() {
		return var->get_location();
	}
	std::ostream &let_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 9;
		parens_t parens(os, parent_precedence, precedence);
		os << "let " << var->get_name() << " = ";
		value->render(os, precedence);
		os << " in ";
		body->render(os, precedence);
		return os;
	}
	location_t literal_t::get_location() {
		return token.location;
	}
	std::ostream &literal_t::render(std::ostream &os, int parent_precedence) {
		return os << token.text;
	}
	location_t conditional_t::get_location() {
		return cond->get_location();
	}
	std::ostream &conditional_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 11;
		parens_t parens(os, parent_precedence, precedence);
		os << C_CONTROL "if " C_RESET;
		cond->render(os, precedence);
		os << C_CONTROL " then " C_RESET;
		truthy->render(os, precedence);
		os << C_CONTROL " else " C_RESET;
		falsey->render(os, precedence);
		return os;
	}
	location_t fix_t::get_location() {
		return f->get_location();
	}
	std::ostream &fix_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 6;
		parens_t parens(os, parent_precedence, precedence);
		os << C_TYPE "fix " C_RESET;
		f->render(os, precedence);
		return os;
	}

	std::ostream &pattern_block_t::render(std::ostream &os) {
		os << "(";
	   	predicate->render(os);
	   	os << " ";
		result->render(os, 0);
		return os << ")";
	}
}

namespace ast {
	int next_fresh = 0;

	std::string fresh() {
		return string_format("__v%d", next_fresh++);
	}
}

#if 0
namespace ast {
	bitter::expr_t::ref and_expr_t::make_expr() {
		return new application(
				bitter::application(
					bitter::var(token),
					lhs->make_expr()),
				rhs->make_expr());
	}

	bitter::expr_t::ref or_expr_t::make_expr() {
		return bitter::application(
				bitter::application(
					bitter::var(token),
					lhs->make_expr()),
				rhs->make_expr());
	}

	bitter::expr_t::ref binary_operator_t::make_expr() {
		return bitter::application(
				bitter::application(
					bitter::var(token),
					lhs->make_expr()),
				rhs->make_expr());
	}

	bitter::expr_t::ref array_index_expr_t::make_expr() {
		if (is_slice) {
			if (stop == nullptr) {
				return bitter::application(
						bitter::application(
							bitter::var("__getslice__/2"),
							lhs->make_expr()),
						start->make_expr());
			} else {
				return bitter::application(
						bitter::application(
							bitter::application(
								bitter::var("__getslice__/3"),
								lhs->make_expr()),
							start->make_expr()),
						stop->make_expr());
			}
		} else {
			return bitter::application(
					bitter::application(
						bitter::var("__getitem__/2"),
						lhs->make_expr()),
					start->make_expr());
		}
	}

	bitter::expr_t::ref array_literal_expr_t::make_expr() {
		auto var = bitter::var(fresh());
		std::vector<bitter::expr_t::ref> exprs;
		for (auto item : items) {
			exprs.push_back(
					bitter::application(
						bitter::application(bitter::var("append"), var),
						item->make_expr()));
		}
		exprs.push_back(var);
		return bitter::let(
				var->var,
				bitter::application(bitter::var("__init_vector__"),
					bitter::literal(token_t{get_location(), tk_integer, string_format("%d", items.size())})),
				bitter::block(exprs));
	}

	bitter::expr_t::ref link_var_statement_t::make_expr() {
		return bitter::literal(token_t{get_location(), tk_string, escape_json_quotes(str())});
	}
	bitter::expr_t::ref link_function_statement_t::make_expr() {
		return bitter::literal(token_t{get_location(), tk_string, escape_json_quotes(str())});
	}
	bitter::expr_t::ref block_t::make_expr() {
		return block_from_block(safe_dyncast<const ast::block_t>(shared_from_this()));
	}
	bitter::expr_t::ref dot_expr_t::make_expr() {
		return bitter::application(
				bitter::var(token_t{rhs.location, tk_identifier, "." + rhs.text}),
				lhs->make_expr());
	}

	bitter::expr_t::ref sizeof_expr_t::make_expr() {
		return bitter::application(
				bitter::var(token_t{token.location, tk_identifier, "sizeof"}),
				bitter::literal(
					token_t{
					parsed_type.type->get_location(),
					tk_string,
					parsed_type.type->repr()}));
	}

	bitter::expr_t::ref prefix_expr_t::make_expr() {
		return bitter::application(
				bitter::var(token),
				rhs->make_expr());
	}

	bitter::expr_t::ref bang_expr_t::make_expr() {
		return bitter::application(
				bitter::var(token),
				lhs->make_expr());
	}

	bitter::pattern_block_t::refs make_pattern_blocks(ast::pattern_block_t::refs pbs) {
		bitter::pattern_block_t::refs blocks;
		for (auto pb : pbs) {
			blocks.push_back(
					std::make_shared<bitter::pattern_block_t>(
						pb->predicate,
						block_from_block(pb->block)));
		}
		return blocks;
	}
	bitter::expr_t::ref match_expr_t::make_expr() {
		return bitter::match(value->make_expr(), make_pattern_blocks(pattern_blocks));
	}

	bitter::expr_t::ref tuple_expr_t::make_expr() {
		assert(values.size() != 0);
		auto app = application(
				bitter::var(token_t{get_location(), tk_string, "__tuple__"}),
				values[0]->make_expr());
		for (int i=1; i<values.size(); ++i) {
			app = bitter::application(app, values[i]->make_expr());
		}
		return app;
	}

	bitter::expr_t::ref literal_expr_t::make_expr() {
		return bitter::var(token);
	}
	bitter::expr_t::ref reference_expr_t::make_expr() {
		return bitter::var(token);
	}
	bitter::expr_t::ref ternary_expr_t::make_expr() {
		return bitter::conditional(condition->make_expr(), when_true->make_expr(), when_false->make_expr());
	}
	bitter::expr_t::ref function_defn_t::make_expr() {
		std::vector<token_t> args = decl->get_arg_tokens();
		if (args.size() == 0) {
			args.push_back(token_t{get_location(), tk_identifier, "_"});
		}
		bitter::expr_t::ref body;
		for (auto iter = args.rbegin(); iter != args.rend(); ++iter) {
			if (body == nullptr) {
				body = bitter::lambda(make_code_id(*iter), block->make_expr());
			} else {
				body = bitter::lambda(make_code_id(*iter), body);
			}
		}
		return body;
	}
	bitter::expr_t::ref typeid_expr_t::make_expr() {
		return application(
				bitter::var(token_t{get_location(), tk_string, "__typeid__"}),
				expr->make_expr());
	}
	bitter::expr_t::ref callsite_expr_t::make_expr() {
		bitter::expr_t::ref operand = (params.size() == 0) ? bitter::unit() : params[0]->make_expr();
		auto app = application(function_expr->make_expr(), operand);
		for (int i=1; i<params.size(); ++i) {
			app = bitter::application(app, params[i]->make_expr());
		}
		return app;
	}
	bitter::expr_t::ref cast_expr_t::make_expr() {
		return as(lhs->make_expr(), parsed_type_cast.type);
	}

	/*
	   bitter::expr_t::ref statement_t::sequence() {
	   return bitter::literal(token_t{INTERNAL_LOC(), tk_string, string_format("\"line %d (%s)\"",
	   get_location().line,
	   token.str().c_str())});
	   }
	   */

	/*************** Statement sequencing ****************/

	std::vector<statement_t::ref>::iterator statement_t::sequence_exprs(
			std::vector<std::shared_ptr<const bitter::expr_t>> &exprs,
			std::vector<statement_t::ref>::iterator next,
			std::vector<statement_t::ref>::iterator end) const
	{
		if (auto expr = dyncast<const expression_t>(shared_from_this())) {
			exprs.push_back(expr->make_expr());
		} else {
			exprs.push_back(
					bitter::literal(
						token_t{
						token.location,
						tk_string,
						string_format("%s", escape_json_quotes(token.location.str().c_str()).c_str())}));
		}
		return next;
	}
	std::vector<statement_t::ref>::iterator break_flow_t::sequence_exprs(
			std::vector<std::shared_ptr<const bitter::expr_t>> &exprs,
			std::vector<statement_t::ref>::iterator next,
			std::vector<statement_t::ref>::iterator end) const
	{
		exprs.push_back(bitter::break_flow(condition_expr->make_expr(), block_from_block(block)));
		return next;
	}
	std::vector<statement_t::ref>::iterator while_block_t::sequence_exprs(
			std::vector<std::shared_ptr<const bitter::expr_t>> &exprs,
			std::vector<statement_t::ref>::iterator next,
			std::vector<statement_t::ref>::iterator end) const
   	{
		auto condition_expr = safe_dyncast<const expression_t>(condition);
		exprs.push_back(bitter::while_loop(condition_expr->make_expr(), block_from_block(block)));
		return next;
	}
	std::vector<statement_t::ref>::iterator return_statement_t::sequence_exprs(
			std::vector<std::shared_ptr<const bitter::expr_t>> &exprs,
			std::vector<statement_t::ref>::iterator next,
			std::vector<statement_t::ref>::iterator end) const
   	{
		exprs.push_back(
				bitter::return_statement(expr != nullptr ? expr->make_expr() : bitter::unit()));
		if (next != end) {
			throw user_error((*next)->get_location(), "this statement will never run");
		}
		return end;
	}
	std::vector<statement_t::ref>::iterator function_defn_t::sequence_exprs(
			std::vector<std::shared_ptr<const bitter::expr_t>> &exprs,
			std::vector<statement_t::ref>::iterator next,
			std::vector<statement_t::ref>::iterator end) const
	{
		if (decl->token.text == "") {
			throw user_error(decl->token.location, "function is instantiated but never assigned a name, so it cannot be used");
		}
		exprs.push_back(
				bitter::let(make_code_id(decl->token),
					make_expr(),
					block_from_statements(next, end)));
		/* assume we ate the rest of the statements */
		return end;
	}
	std::vector<statement_t::ref>::iterator var_decl_t::sequence_exprs(
			std::vector<std::shared_ptr<const bitter::expr_t>> &exprs,
			std::vector<statement_t::ref>::iterator next,
			std::vector<statement_t::ref>::iterator end) const
	{
		exprs.push_back(
				bitter::let(
					make_code_id(token),
					(initializer != nullptr)
					? initializer->make_expr()
					: bitter::application(bitter::var("__init__"), bitter::unit()),
					block_from_statements(next, end)));

		/* assume we ate the rest of the statements */
		return end;
	}
	std::vector<statement_t::ref>::iterator if_block_t::sequence_exprs(
			std::vector<std::shared_ptr<const bitter::expr_t>> &exprs,
			std::vector<statement_t::ref>::iterator next,
			std::vector<statement_t::ref>::iterator end) const
	{
		assert(next == end || *next != shared_from_this());

		auto condition_expr = safe_dyncast<const expression_t>(condition);
		exprs.push_back(
				bitter::conditional(
					condition_expr->make_expr(),
					block_from_block(block),
					block_from_statement(else_)));
		return next;
	}
}

bitter::block_t::ref block_from_block(ast::block_t::ref block) {
	if (block == nullptr) {
		return bitter::block({bitter::unit()});
	} else {
		return block_from_statements(block->statements);
	}
}

bitter::block_t::ref block_from_statement(std::shared_ptr<const ast::statement_t> stmt) {
	std::vector<std::shared_ptr<const ast::statement_t>> stmts;
	if (stmt != nullptr) {
		stmts.push_back(stmt);
	}

	return block_from_statements(stmts);
}

bitter::block_t::ref block_from_statements(std::vector<std::shared_ptr<const ast::statement_t>> stmts) {
	return block_from_statements(stmts.begin(), stmts.end());
}

bitter::block_t::ref block_from_statements(
		std::vector<std::shared_ptr<const ast::statement_t>>::iterator begin,
		std::vector<std::shared_ptr<const ast::statement_t>>::iterator end)
{
	std::vector<bitter::expr_t::ref> exprs = ::sequence_exprs(begin, end);

	if (exprs.size() == 0) {
		return bitter::block({bitter::unit()});
	} else {
		return bitter::block(exprs);
	}
}

std::vector<std::shared_ptr<const bitter::expr_t>> sequence_exprs(
		std::vector<std::shared_ptr<const ast::statement_t>>::iterator next,
		std::vector<std::shared_ptr<const ast::statement_t>>::iterator end)
{
	std::vector<std::shared_ptr<const bitter::expr_t>> exprs;
	while (next != end) {
		ast::statement_t::ref statement = *next;
		debug_above(8, log("emitting statement %s", statement->str().c_str()));
		++next;
		next = statement->sequence_exprs(exprs, next, end);
	}
	assert(next == end);
	return exprs;
}
#endif
