#include "bitter.h"
#include "parens.h"
#include "ast.h"
#include "code_id.h"

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
		if (parent_precedence != 0) {
			os << "(";
		}
		a->render(os, precedence);
		os << " ";
		b->render(os, precedence);
		if (parent_precedence != 0) {
			os << ")";
		}
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
		body->render(os, precedence);
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
	as_t::ref as(expr_t::ref expr, types::type_t::ref type) {
		return std::make_shared<as_t>(expr, type);
	}
	block_t::ref block(const std::vector<expr_t::ref> &statements) {
		return std::make_shared<block_t>(statements);
	}
	literal_t::ref literal(token_t token) {
		return std::make_shared<literal_t>(token);
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
		return std::make_shared<decl_t>(var, value);
	}
	decl_t::ref decl(token_t var, expr_t::ref value) {
		return std::make_shared<decl_t>(make_code_id(var), value);
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

	bitter::expr_t::ref if_block_t::make_expr() const {
		auto condition_expr = safe_dyncast<const expression_t>(condition);
		return bitter::conditional(condition_expr->make_expr(), block->make_expr(), else_ ? else_->make_expr() : bitter::unit());
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

	bitter::expr_t::ref statement_t::make_expr() const {
		return bitter::literal(token_t{INTERNAL_LOC(), tk_string, string_format("\"line %d (%s)\"",
				   	get_location().line,
					token.str().c_str())});
	}

	template <typename ITER>
		std::vector<bitter::expr_t::ref> append_statements(
				ITER iter,
				ITER end)
		{
			std::vector<bitter::expr_t::ref> exprs;
			for (;iter != end; ++iter) {
				if (auto var_decl = dyncast<const var_decl_t>(*iter)) {
					++iter;
					exprs.push_back(
							bitter::let(
								make_code_id(var_decl->token),
								(var_decl->initializer != nullptr)
								? var_decl->initializer->make_expr()
								: bitter::application(bitter::var("__init__"), bitter::unit()),
								block(append_statements(iter, end))));
					return exprs;
				} else {
					exprs.push_back((*iter)->make_expr());
				}
			}
			return exprs;
		}

	bitter::expr_t::ref block_t::make_expr() const {
		return block(append_statements(statements.begin(), statements.end()));
	}
}

