#pragma once
#include <vector>
#include <memory>
#include "token.h"
#include <iostream>

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
		using ref = std::shared_ptr<expr_t>;

		virtual ~expr_t() throw() {}
		virtual location_t get_location() const = 0;
		virtual std::ostream &render(std::ostream &os, int parent_precedence) const = 0;
	};

	struct var_t : public expr_t {
		var_t(token_t var) : var(var) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;

		token_t var;
	};

	struct application_t : public expr_t {
		application_t(expr_t::ref a, expr_t::ref b) : a(a), b(b) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;
		expr_t::ref a, b;
	};

	struct lambda_t : public expr_t {
		lambda_t(token_t var, expr_t::ref body) : var(var), body(body) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;

		token_t var;
		expr_t::ref body;
	};

	struct let_t : public expr_t {
		let_t(token_t var, expr_t::ref value, expr_t::ref body): var(var), value(value), body(body) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;

		token_t var;
		expr_t::ref value, body;
	};

	struct literal_t : public expr_t {
		literal_t(token_t value) : value(value) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;

		token_t value;
	};

	struct conditional_t : public expr_t {
		conditional_t(expr_t::ref cond, expr_t::ref truthy, expr_t::ref falsey): cond(cond), truthy(truthy), falsey(falsey) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;

		expr_t::ref cond, truthy, falsey;
	};

	struct fix_t : public expr_t {
		fix_t(expr_t::ref f): f(f) {}
		location_t get_location() const override;
		std::ostream &render(std::ostream &os, int parent_precedence) const override;

		expr_t::ref f;
	};

	struct decl_t {
		using ref = std::shared_ptr<decl_t>;

		decl_t(token_t var, expr_t::ref value) : var(var), value(value) {}

		token_t var;
		expr_t::ref value;
	};

	struct program_t {
		using ref = std::shared_ptr<program_t>;
		std::vector<decl_t::ref> decls;
	};
}

std::ostream &operator <<(std::ostream &os, const bitter::program_t &program);
std::ostream &operator <<(std::ostream &os, const bitter::decl_t &decl);
