#include "bitter.h"
#include "parens.h"
#include "ast.h"

std::ostream &operator <<(std::ostream &os, const bitter::program_t &program) {
	os << "program";
	const char *delim = "\n";
	for (auto decl : program.decls) {
		os << delim << *decl;
	}
	return os << std::endl;
}

std::ostream &operator <<(std::ostream &os, const bitter::decl_t &decl) {
	os << decl.var.text << " = ";
	return decl.value->render(os, 0);
}

namespace bitter {
	location_t var_t::get_location() const {
		return var.location;
	}
	std::ostream &var_t::render(std::ostream &os, int parent_precedence) const {
		return os << var.text;
	}
	location_t application_t::get_location() const {
		return a->get_location();
	}
	std::ostream &application_t::render(std::ostream &os, int parent_precedence) const {
		const int precedence = 5;
		os << "(";
		a->render(os, precedence);
	   	os << " ";
	   	b->render(os, precedence);
	   	return os << ")";
	}
	location_t lambda_t::get_location() const {
		return var.location;
	}
	std::ostream &lambda_t::render(std::ostream &os, int parent_precedence) const {
		const int precedence = 7;
		os << "(λ" << var.text << ".";
	   	body->render(os, precedence);
	   	return os << ")";
	}
	location_t let_t::get_location() const {
		return var.location;
	}
	std::ostream &let_t::render(std::ostream &os, int parent_precedence) const {
		const int precedence = 9;
		parens_t parens(os, parent_precedence, precedence);
		os << "let " << var.text << " = ";
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
		os << "if ";
	   	cond->render(os, precedence);
	   	os << " then ";
	   	truthy->render(os, precedence);
	   	os << " else ";
	   	falsey->render(os, precedence);
	   	return os;
	}
	location_t fix_t::get_location() const {
		return f->get_location();
	}
	std::ostream &fix_t::render(std::ostream &os, int parent_precedence) const {
		const int precedence = 6;
		parens_t parens(os, parent_precedence, precedence);
		os << "fix ";
	   	f->render(os, precedence);
	   	return os;
	}
}

namespace ast {
	int next_fresh = 0;
	std::string fresh() {
		return string_format("__v%d", next_fresh++);
	}

	bitter::expr_t::ref statement_t::make_expr() const {
		return std::make_shared<bitter::literal_t>(token_t{INTERNAL_LOC(), tk_string, string_format("\"line %d\"", get_location().line)});
	}

	bitter::expr_t::ref block_t::make_expr() const {
		// ((\dummy.last) first)
		bitter::expr_t::ref body;
		for (auto statement : statements) {
			if (body == nullptr) {
				body = statement->make_expr();
			} else {
				body = std::make_shared<bitter::application_t>(
						std::make_shared<bitter::lambda_t>(token_t{INTERNAL_LOC(), tk_identifier, fresh()},
							statement->make_expr()),
						body);
			}
		}
		return body;
	}
}

