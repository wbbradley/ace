#pragma once
#include <vector>
#include <memory>
#include "identifier.h"
#include "token.h"
#include <iostream>
#include "types.h"

enum expr_kind_t {
	var,
	app,
	lam,
	let,
	lit,
	cond,
	fix,
	op,
};

namespace bitter {
	struct expr_t {
		using ref = std::shared_ptr<const expr_t>;

		virtual ~expr_t() throw() {}
		virtual location_t get_location() const = 0;
		virtual std::ostream &render(std::ostream &os, int parent_precedence) const = 0;
	};

	struct var_t : public expr_t {
		using ref = std::shared_ptr<const var_t>;
		var_t(identifier::ref var) : var(var) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;

		identifier::ref var;
	};

	struct block_t : public expr_t {
		using ref = std::shared_ptr<const block_t>;
		block_t(std::vector<expr_t::ref> statements) : statements(statements) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;

		std::vector<expr_t::ref> statements;
	};

	struct as_t : public expr_t {
		using ref = std::shared_ptr<const as_t>;
		as_t(expr_t::ref expr, types::type_t::ref type) : expr(expr), type(type) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;

		expr_t::ref expr;
		types::type_t::ref type;
	};

	struct application_t : public expr_t {
		using ref = std::shared_ptr<const application_t>;
		application_t(expr_t::ref a, expr_t::ref b) : a(a), b(b) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;
		expr_t::ref a, b;
	};

	struct lambda_t : public expr_t {
		using ref = std::shared_ptr<const lambda_t>;
		lambda_t(identifier::ref var, expr_t::ref body) : var(var), body(body) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;

		identifier::ref var;
		expr_t::ref body;
	};

	struct let_t : public expr_t {
		using ref = std::shared_ptr<const let_t>;
		let_t(identifier::ref var, expr_t::ref value, expr_t::ref body): var(var), value(value), body(body) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;

		identifier::ref var;
		expr_t::ref value, body;
	};

	struct literal_t : public expr_t {
		using ref = std::shared_ptr<const literal_t>;
		literal_t(token_t value) : value(value) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;

		token_t value;
	};

	struct conditional_t : public expr_t {
		using ref = std::shared_ptr<const conditional_t>;
		conditional_t(expr_t::ref cond, expr_t::ref truthy, expr_t::ref falsey): cond(cond), truthy(truthy), falsey(falsey) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;

		expr_t::ref cond, truthy, falsey;
	};

	struct fix_t : public expr_t {
		using ref = std::shared_ptr<const fix_t>;
		fix_t(expr_t::ref f): f(f) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;

		expr_t::ref f;
	};

	struct decl_t {
		using ref = std::shared_ptr<const decl_t>;

		decl_t(identifier::ref var, expr_t::ref value) : var(var), value(value) {}

		identifier::ref var;
		expr_t::ref value;
	};

	struct program_t {
		using ref = std::shared_ptr<program_t>;
		program_t(std::vector<decl_t::ref> decls, expr_t::ref expr) : decls(decls), expr(expr) {}
		std::vector<decl_t::ref> decls;
		expr_t::ref expr;
	};

	var_t::ref unit();
	var_t::ref var(identifier::ref name);
	var_t::ref var(std::string name);
	var_t::ref var(std::string name, location_t location);
	var_t::ref var(token_t token);
	as_t::ref as(expr_t::ref expr, types::type_t::ref type);
	block_t::ref block(const std::vector<expr_t::ref> &statements);
	literal_t::ref literal(token_t token);
	application_t::ref application(expr_t::ref a, expr_t::ref b);
	lambda_t::ref lambda(identifier::ref var, expr_t::ref body);
	let_t::ref let(identifier::ref var, expr_t::ref value, expr_t::ref body);
	conditional_t::ref conditional(expr_t::ref cond, expr_t::ref truthy, expr_t::ref falsey);
	fix_t::ref fix(expr_t::ref f);
	decl_t::ref decl(identifier::ref var, expr_t::ref value);
	decl_t::ref decl(token_t token, expr_t::ref value);
	program_t::ref program(std::vector<decl_t::ref> decls, expr_t::ref expr);
}

std::ostream &operator <<(std::ostream &os, const bitter::program_t &program);
std::ostream &operator <<(std::ostream &os, const bitter::decl_t &decl);
