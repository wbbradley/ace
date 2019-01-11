#include "parens.h"
#include "ast.h"

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

std::ostream &operator <<(std::ostream &os, bitter::expr_t *expr) {
	return expr->render(os, 0);
}

namespace bitter {
	std::string expr_t::str() {
		std::stringstream ss;
		this->render(ss, 0);
		return ss.str();
	}
	location_t var_t::get_location() {
		return id.location;
	}
	std::ostream &var_t::render(std::ostream &os, int parent_precedence) {
		return os << id.name;
	}
	location_t as_t::get_location() {
		return scheme->get_location();
	}
	std::ostream &as_t::render(std::ostream &os, int parent_precedence) {
		os << "(";
		expr->render(os, 10);
		if (force_cast) {
			os << C_WARN " as! " C_RESET;
		} else {
			os << C_TYPE " as " C_RESET;
		}
		os << scheme->str();
		os << ")";
		return os;
	}
	location_t sizeof_t::get_location() {
		return location;
	}
	std::ostream &sizeof_t::render(std::ostream &os, int parent_precedence) {
		return os << "sizeof(" << type->str() << ")";
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
		os << "{";
		for (auto statement: statements) {
			os << delim;
			statement->render(os, precedence);
			delim = "; ";
		}
		return os << "}";
	}
	location_t lambda_t::get_location() {
		return var.location;
	}
	std::ostream &lambda_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 7;
		os << "(λ" << var.name;
		if (param_type != nullptr) {
			os << c_good(" :: ");
			os << C_TYPE;
			param_type->emit(os, {}, 0);
			os << C_RESET " ";
		}
		os << ".";
		body->render(os, 0);
		os << ")";
		if (return_type != nullptr) {
			os << c_good(" -> ");
			os << C_TYPE;
			return_type->emit(os, {}, 0);
			os << C_RESET;
		}
		return os;
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
	location_t literal_t::get_location() const {
		return token.location;
	}
	std::ostream &literal_t::render(std::ostream &os, int parent_precedence) {
		return os << token.text;
	}
	std::ostream &literal_t::render(std::ostream &os) const {
		return os << token.text;
	}
	location_t tuple_t::get_location() {
		return location;
	}
	std::ostream &tuple_t::render(std::ostream &os, int parent_precedence) {
		os << "(";
		int i = 0;
		const char *delim = "";
		for (auto dim : dims) {
			os << delim;
			dim->render(os, 0);
			delim = ", ";
		}
		if (dims.size() == 1) {
			os << ",";
		}
		return os << ")";
	}
	location_t tuple_deref_t::get_location() {
		return expr->get_location();
	}
	std::ostream &tuple_deref_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 20;
		expr->render(os, precedence);
		return os << "[" << index << " of " << max << "]";
	}
	location_t conditional_t::get_location() {
		return cond->get_location();
	}
	std::ostream &conditional_t::render(std::ostream &os, int parent_precedence) {
		const int precedence = 11;
		parens_t parens(os, parent_precedence, precedence);
		os << "(" C_CONTROL "if " C_RESET;
		cond->render(os, precedence);
		os << C_CONTROL " then " C_RESET;
		truthy->render(os, precedence);
		os << C_CONTROL " else " C_RESET;
		falsey->render(os, precedence);
		return os << ")";
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
	std::string predicate_t::str() const {
		std::stringstream ss;
		this->render(ss);
		return ss.str();
	}
	std::ostream &ctor_predicate_t::render(std::ostream &os) const {
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

	location_t ctor_predicate_t::get_location() const {
		return location;
	}

	std::ostream &tuple_predicate_t::render(std::ostream &os) const {
		os << "(";
		const char *delim = "";
		for (auto predicate : params) {
			os << delim;
			predicate->render(os);
			delim = ", ";
		}
		return os << ")";
	}

	location_t tuple_predicate_t::get_location() const {
		return location;
	}

	std::ostream &irrefutable_predicate_t::render(std::ostream &os) const {
		return os << C_ID << (name_assignment.valid ? name_assignment.t.name : "_") << C_RESET;
	}

	location_t irrefutable_predicate_t::get_location() const {
		return location;
	}

	types::type_t::ref type_decl_t::get_type() const {
		std::vector<types::type_t::ref> types;
		assert(isupper(id.name[0]));
		types.push_back(type_id(id));
		for (auto param : params) {
			assert(islower(param.name[0]));
			types.push_back(type_variable(param));
		}
		if (types.size() >= 2) {
			return type_operator(types);
		} else {
			return types[0];
		}
	}

	std::string decl_t::str() const {
		std::stringstream ss;
		ss << "let " << var << " = ";
		value->render(ss, 0);
		return ss.str();
	}

	location_t decl_t::get_location() const {
		return var.location;
	}

	std::string type_class_t::str() const {
		return string_format("class %s %s {\n\t%s%s\n}",
				id.name.c_str(),
				type_var_id.name.c_str(),
				superclasses.size() != 0 ? string_format("has %s\n\t", join(superclasses, ", ").c_str()).c_str() : "",
				::str(overloads).c_str());
	}

	location_t type_class_t::get_location() const {
		return id.location;
	}

	std::string instance_t::str() const {
		return string_format("instance %s %s {\n\t%s\n}",
				type_class_id.name.c_str(),
				type->str().c_str(),
				::join_str(decls, "\n\t").c_str());
	}

	location_t instance_t::get_location() const {
		return type_class_id.location;
	}
}

bitter::expr_t *unit_expr(location_t location) {
	return new bitter::tuple_t(location, {});
}

namespace bitter {
	int next_fresh = 0;

	std::string fresh() {
		return string_format("__v%d", next_fresh++);
	}
}
