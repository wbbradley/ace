#include "parens.h"
#include "ast.h"

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
	os << decl->var.name << " = ";
	return decl->value->render(os, 0);
}

namespace bitter {
	location_t var_t::get_location() {
		return id.location;
	}
	std::ostream &var_t::render(std::ostream &os, int parent_precedence) {
		return os << id.name;
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
				if (strspn(oper->id.name.c_str(), MATHY_SYMBOLS) == oper->id.name.size()) {
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
	location_t continue_t::get_location() {
		return location;
	}
	std::ostream &continue_t::render(std::ostream &os, int parent_precedence) {
		return os << "(" C_CONTROL "continue!" C_RESET ")";
	}
	location_t break_t::get_location() {
		return location;
	}
	std::ostream &break_t::render(std::ostream &os, int parent_precedence) {
		return os << "(" C_CONTROL "break!" C_RESET ")";
	}
	location_t return_statement_t::get_location() {
		return value->get_location();
	}
	std::ostream &return_statement_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 4;
		parens_t parens(os, parent_precedence, precedence);
		os << C_CONTROL "return! " C_RESET;
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
		return var.location;
	}
	std::ostream &lambda_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 7;
		os << "(λ" << var.name << ".";
		body->render(os, 0);
		return os << ")";
	}
	location_t let_t::get_location() {
		return var.location;
	}
	std::ostream &let_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 9;
		parens_t parens(os, parent_precedence, precedence);
		os << "let " << var.name << " = ";
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
	std::ostream &literal_t::render(std::ostream &os) {
		return os << token.text;
	}
	location_t tuple_t::get_location() {
		return location;
	}
	std::ostream &tuple_t::render(std::ostream &os, int parent_precedence) {
		os << "(";
		int i = 0;
		for (auto dim : dims) {
			dim->render(os, 0);
			if (dims.size() - 1 != i || i == 0) {
				os << ", ";
			}
		}
		return os << ")";
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
	std::ostream &ctor_predicate_t::render(std::ostream &os) {
		os << C_ID << ctor_name.name << C_RESET;
		if (params.size() != 0) {
			os << "(";
			const char *delim = "";
			for (auto predicate : params) {
				os << delim;
				predicate->render(os);
				delim = ", ";
			}
			os << ")";
		}
		return os;
	}

	std::ostream &tuple_predicate_t::render(std::ostream &os) {
		os << "(";
		const char *delim = "";
		for (auto predicate : params) {
			os << delim;
			predicate->render(os);
			delim = ", ";
		}
		return os << ")";
	}

	std::ostream &irrefutable_predicate_t::render(std::ostream &os) {
		return os << C_ID << (name_assignment.valid ? name_assignment.t.name : "_") << C_RESET;
	}
}

namespace bitter {
	int next_fresh = 0;

	std::string fresh() {
		return string_format("__v%d", next_fresh++);
	}
}
