#include "bitter.h"
#include "parens.h"
#include "ast.h"
#include "code_id.h"

#define MATHY_SYMBOLS "!@#$%^&*()+-_=><.,/|[]`~\\"

std::ostream &operator <<(std::ostream &os, const bitter::program_t &program) {
	os << "program";
	const char *delim = "\n";
	for (auto decl : program.decls) {
		os << delim << *decl;
	}
	return os << std::endl;
}

std::ostream &operator <<(std::ostream &os, const bitter::decl_t &decl) {
	os << decl.var->get_name() << " = ";
	return decl.value->render(os, 0);
}

namespace bitter {
	location_t var_t::get_location() const {
		return var->get_location();
	}
	std::ostream &var_t::render(std::ostream &os, int parent_precedence) const {
		return os << var->get_name();
	}
	location_t as_t::get_location() const {
		return type->get_location();
	}
	std::ostream &as_t::render(std::ostream &os, int parent_precedence) const {
		os << "(";
		expr->render(os, 10);
		os << C_TYPE " as " C_RESET;
		type->emit(os, {}, 0);
		os << ")";
		return os;
	}
	location_t application_t::get_location() const {
		return a->get_location();
	}
	std::ostream &application_t::render(std::ostream &os, int parent_precedence) const {
		const int precedence = 5;
		if (auto inner_app = dyncast<const application_t>(a)) {
			if (auto oper = dyncast<const var_t>(inner_app->a)) {
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
	location_t return_statement_t::get_location() const {
		return value->get_location();
	}
	std::ostream &return_statement_t::render(std::ostream &os, int parent_precedence) const {
		const int precedence = 4;
		parens_t parens(os, parent_precedence, precedence);
		os << C_CONTROL "return " C_RESET;
		value->render(os, 0);
		return os;
	}
	location_t match_t::get_location() const {
		return scrutinee->get_location();
	}
	std::ostream &match_t::render(std::ostream &os, int parent_precedence) const {
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
	location_t while_t::get_location() const {
		return condition->get_location();
	}
	std::ostream &while_t::render(std::ostream &os, int parent_precedence) const {
		const int precedence = 3;
		parens_t parens(os, parent_precedence, precedence);
		os << C_CONTROL "while " C_RESET;
		condition->render(os, 6);
		os << " ";
		block->render(os, precedence);
		return os;
	}
	location_t block_t::get_location() const {
		assert(statements.size() != 0);
		return statements[0]->get_location();
	}
	std::ostream &block_t::render(std::ostream &os, int parent_precedence) const {
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
	location_t lambda_t::get_location() const {
		return var->get_location();
	}
	std::ostream &lambda_t::render(std::ostream &os, int parent_precedence) const {
		const int precedence = 7;
		os << "(λ" << var->get_name() << ".";
		body->render(os, 0);
		return os << ")";
	}
	location_t let_t::get_location() const {
		return var->get_location();
	}
	std::ostream &let_t::render(std::ostream &os, int parent_precedence) const {
		const int precedence = 9;
		parens_t parens(os, parent_precedence, precedence);
		os << "let " << var->get_name() << " = ";
		value->render(os, precedence);
		os << " in ";
		body->render(os, precedence);
		return os;
	}
	location_t literal_t::get_location() const {
		return value.location;
	}
	std::ostream &literal_t::render(std::ostream &os, int parent_precedence) const {
		return os << value.text;
	}
	location_t conditional_t::get_location() const {
		return cond->get_location();
	}
	std::ostream &conditional_t::render(std::ostream &os, int parent_precedence) const {
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
	location_t fix_t::get_location() const {
		return f->get_location();
	}
	std::ostream &fix_t::render(std::ostream &os, int parent_precedence) const {
		const int precedence = 6;
		parens_t parens(os, parent_precedence, precedence);
		os << C_TYPE "fix " C_RESET;
		f->render(os, precedence);
		return os;
	}

	std::ostream &pattern_block_t::render(std::ostream &os) const {
		os << "(" << predicate->repr() << " ";
		result->render(os, 0);
		return os << ")";
	}
	var_t::ref unit() {
		return var("unit");
	}
	var_t::ref var(std::string name) {
		return std::make_shared<var_t>(make_iid(name));
	}
	var_t::ref var(identifier::ref name) {
		return std::make_shared<var_t>(name);
	}
	var_t::ref var(std::string name, location_t location) {
		return std::make_shared<var_t>(make_iid_impl(name, location));
	}
	var_t::ref var(token_t token) {
		return std::make_shared<var_t>(make_code_id(token));
	}
	while_t::ref while_loop(expr_t::ref condition, block_t::ref block) {
		return std::make_shared<while_t>(condition, block);
	}
	match_t::ref match(expr_t::ref value, pattern_block_t::refs pattern_blocks) {
		return std::make_shared<match_t>(value, pattern_blocks);
	}
	as_t::ref as(expr_t::ref expr, types::type_t::ref type) {
		return std::make_shared<as_t>(expr, type);
	}
	block_t::ref block(const std::vector<expr_t::ref> &statements) {
		return std::make_shared<block_t>(statements);
	}
	literal_t::ref literal(token_t token) {
		return std::make_shared<literal_t>(token);
	}
	return_statement_t::ref return_statement(expr_t::ref value) {
		return std::make_shared<return_statement_t>(value);
	}
	application_t::ref application(expr_t::ref a, expr_t::ref b) {
		return std::make_shared<application_t>(a, b);
	}
	lambda_t::ref lambda(identifier::ref var, expr_t::ref body) {
		return std::make_shared<lambda_t>(var, body);
	}
	let_t::ref let(identifier::ref var, expr_t::ref value, expr_t::ref body) {
		return std::make_shared<let_t>(var, value, body);
	}
	conditional_t::ref conditional(expr_t::ref cond, expr_t::ref truthy, expr_t::ref falsey) {
		return std::make_shared<conditional_t>(cond, truthy, falsey);
	}
	fix_t::ref fix(expr_t::ref f) {
		return std::make_shared<fix_t>(f);
	}
	decl_t::ref decl(identifier::ref var, expr_t::ref value) {
		assert(var != nullptr);
		assert(value != nullptr);
		return std::make_shared<decl_t>(var, value);
	}
	decl_t::ref decl(token_t var, expr_t::ref value) {
		return decl(make_code_id(var), value);
	}
	program_t::ref program(std::vector<decl_t::ref> decls, expr_t::ref expr) {
		return std::make_shared<program_t>(decls, expr);
	}
}

namespace ast {
	int next_fresh = 0;

	std::string fresh() {
		return string_format("__v%d", next_fresh++);
	}

	bitter::expr_t::ref and_expr_t::make_expr() const {
		return bitter::application(
				bitter::application(
					bitter::var(token),
					lhs->make_expr()),
				rhs->make_expr());
	}

	bitter::expr_t::ref or_expr_t::make_expr() const {
		return bitter::application(
				bitter::application(
					bitter::var(token),
					lhs->make_expr()),
				rhs->make_expr());
	}

	bitter::expr_t::ref binary_operator_t::make_expr() const {
		return bitter::application(
				bitter::application(
					bitter::var(token),
					lhs->make_expr()),
				rhs->make_expr());
	}

	bitter::expr_t::ref array_index_expr_t::make_expr() const {
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

	bitter::expr_t::ref array_literal_expr_t::make_expr() const {
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

	bitter::expr_t::ref link_var_statement_t::make_expr() const {
		return bitter::literal(token_t{get_location(), tk_string, escape_json_quotes(str())});
	}
	bitter::expr_t::ref link_function_statement_t::make_expr() const {
		return bitter::literal(token_t{get_location(), tk_string, escape_json_quotes(str())});
	}
	bitter::expr_t::ref block_t::make_expr() const {
		return block_from_block(safe_dyncast<const ast::block_t>(shared_from_this()));
	}
	bitter::expr_t::ref dot_expr_t::make_expr() const {
		return bitter::application(
				bitter::var(token_t{rhs.location, tk_identifier, "." + rhs.text}),
				lhs->make_expr());
	}

	bitter::expr_t::ref sizeof_expr_t::make_expr() const {
		return bitter::application(
				bitter::var(token_t{token.location, tk_identifier, "sizeof"}),
				bitter::literal(
					token_t{
					parsed_type.type->get_location(),
					tk_string,
					parsed_type.type->repr()}));
	}

	bitter::expr_t::ref prefix_expr_t::make_expr() const {
		return bitter::application(
				bitter::var(token),
				rhs->make_expr());
	}

	bitter::expr_t::ref bang_expr_t::make_expr() const {
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
	bitter::expr_t::ref match_expr_t::make_expr() const {
		return bitter::match(value->make_expr(), make_pattern_blocks(pattern_blocks));
	}

	bitter::expr_t::ref tuple_expr_t::make_expr() const {
		assert(values.size() != 0);
		auto app = application(
				bitter::var(token_t{get_location(), tk_string, "__tuple__"}),
				values[0]->make_expr());
		for (int i=1; i<values.size(); ++i) {
			app = bitter::application(app, values[i]->make_expr());
		}
		return app;
	}

	bitter::expr_t::ref literal_expr_t::make_expr() const {
		return bitter::var(token);
	}
	bitter::expr_t::ref reference_expr_t::make_expr() const {
		return bitter::var(token);
	}
	bitter::expr_t::ref ternary_expr_t::make_expr() const {
		return bitter::conditional(condition->make_expr(), when_true->make_expr(), when_false->make_expr());
	}
	bitter::expr_t::ref function_defn_t::make_expr() const {
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
	bitter::expr_t::ref typeid_expr_t::make_expr() const {
		return application(
				bitter::var(token_t{get_location(), tk_string, "__typeid__"}),
				expr->make_expr());
	}
	bitter::expr_t::ref callsite_expr_t::make_expr() const {
		bitter::expr_t::ref operand = (params.size() == 0) ? bitter::unit() : params[0]->make_expr();
		auto app = application(function_expr->make_expr(), operand);
		for (int i=1; i<params.size(); ++i) {
			app = bitter::application(app, params[i]->make_expr());
		}
		return app;
	}
	bitter::expr_t::ref cast_expr_t::make_expr() const {
		return as(lhs->make_expr(), parsed_type_cast.type);
	}

	/*
	   bitter::expr_t::ref statement_t::sequence() const {
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

