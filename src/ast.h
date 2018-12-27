#pragma once
#include <vector>
#include "identifier.h"
#include "token.h"
#include <iostream>
#include "types.h"
#include "match.h"
#include "env.h"

namespace bitter {
	std::string fresh();

	struct expr_t {
		virtual ~expr_t() throw() {}
		virtual location_t get_location() = 0;
		virtual std::ostream &render(std::ostream &os, int parent_precedence) = 0;
		std::string str();
	};

	struct var_t : public expr_t {
		var_t(identifier_t id) : id(id) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;

		identifier_t const id;
	};

	struct pattern_block_t {
		pattern_block_t(predicate_t *predicate, expr_t *result) : predicate(predicate), result(result) {}
		std::ostream &render(std::ostream &os);

		predicate_t * const predicate;
		expr_t * const result;
	};

	using pattern_blocks_t = std::vector<pattern_block_t *>;
	struct match_t : public expr_t {
		match_t(expr_t *scrutinee, pattern_blocks_t pattern_blocks) : scrutinee(scrutinee), pattern_blocks(pattern_blocks) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;

		expr_t * const scrutinee;
		pattern_blocks_t const pattern_blocks;
	};

	struct predicate_t {
		virtual ~predicate_t() {}
		virtual std::ostream &render(std::ostream &os) = 0;
		virtual match::Pattern::ref get_pattern(types::type_t::ref type, env_ref_t env) const = 0;
	};

	struct tuple_predicate_t : public predicate_t {
		tuple_predicate_t(location_t location, std::vector<predicate_t *> params, maybe<identifier_t> name_assignment) :
			location(location), params(params), name_assignment(name_assignment) {}
		std::ostream &render(std::ostream &os) override;
		match::Pattern::ref get_pattern(types::type_t::ref type, env_ref_t env) const override;

		location_t const location;
		std::vector<predicate_t *> const params;
		maybe<identifier_t> const name_assignment;
	};

	struct irrefutable_predicate_t : public predicate_t {
		irrefutable_predicate_t(location_t location, maybe<identifier_t> name_assignment) : location(location), name_assignment(name_assignment) {}
		std::ostream &render(std::ostream &os) override;
		match::Pattern::ref get_pattern(types::type_t::ref type, env_ref_t env) const override;

		location_t const location;
		maybe<identifier_t> const name_assignment;
	};

	struct ctor_predicate_t : public predicate_t {
		ctor_predicate_t(
				location_t location,
				std::vector<predicate_t *> params,
				identifier_t ctor_name,
				maybe<identifier_t> name_assignment) :
			location(location), params(params), ctor_name(ctor_name), name_assignment(name_assignment) {}
		std::ostream &render(std::ostream &os) override;
		match::Pattern::ref get_pattern(types::type_t::ref type, env_ref_t env) const override;

		location_t const location;
		std::vector<predicate_t *> const params;
		identifier_t const ctor_name;
		maybe<identifier_t> const name_assignment;
	};

	struct block_t : public expr_t {
		block_t(std::vector<expr_t *> statements) : statements(statements) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;

		std::vector<expr_t *> const statements;
	};

	struct as_t : public expr_t {
		as_t(expr_t *expr, types::type_t::ref type, bool force_cast) : expr(expr), type(type), force_cast(force_cast) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;

		expr_t * const expr;
		types::type_t::ref const type;
		bool force_cast;
	};

	struct application_t : public expr_t {
		application_t(expr_t *a, expr_t *b) : a(a), b(b) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;
		expr_t * const a;
		expr_t * const b;
	};

	struct lambda_t : public expr_t {
		lambda_t(identifier_t var, types::type_t::ref param_type, types::type_t::ref return_type, expr_t *body) : var(var), param_type(param_type), return_type(return_type), body(body) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;

		identifier_t const var;
		expr_t * const body;
		types::type_t::ref const param_type;
		types::type_t::ref const return_type;
	};

	struct let_t : public expr_t {
		let_t(identifier_t var, expr_t *value, expr_t *body): var(var), value(value), body(body) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;

		identifier_t const var;
		expr_t * const value;
		expr_t * const body;
	};

	struct tuple_t : public expr_t {
		tuple_t(location_t location, std::vector<expr_t *> dims) : location(location), dims(dims) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;

		location_t const location;
		std::vector<expr_t *> const dims;
	};

	struct literal_t : public expr_t, public predicate_t {
		literal_t(token_t token) : token(token) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;

		std::ostream &render(std::ostream &os) override;
		match::Pattern::ref get_pattern(types::type_t::ref type, env_ref_t env) const override;

		token_t const token;
	};

	struct conditional_t : public expr_t {
		conditional_t(expr_t *cond, expr_t *truthy, expr_t *falsey): cond(cond), truthy(truthy), falsey(falsey) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;

		expr_t * const cond, * const truthy, * const falsey;
	};
	
	struct return_statement_t : public expr_t {
		return_statement_t(expr_t *value) : value(value) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;

		expr_t * const value;
	};

	struct continue_t : public expr_t {
		continue_t(location_t location) : location(location) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;
		location_t location;
	};

	struct break_t : public expr_t {
		break_t(location_t location) : location(location) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;
		location_t location;
	};

	struct while_t : public expr_t {
		while_t(expr_t *condition, expr_t *block) : condition(condition), block(block) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;

		expr_t * const condition, * const block;
	};

	struct fix_t : public expr_t {
		fix_t(expr_t *f): f(f) {}
		location_t get_location() override;
		std::ostream &render(std::ostream &os, int parent_precedence) override;

		expr_t * const f;
	};

	struct decl_t {
		decl_t(identifier_t var, expr_t *value) : var(var), value(value) {}
		std::string str() const;
		location_t get_location() const;

		identifier_t const var;
		expr_t * const value;
	};

	struct type_decl_t {
		type_decl_t(identifier_t id, const identifiers_t &params) : id(id), params(params) {}

		identifier_t const id;
		identifiers_t const params;
		types::type_t::ref get_type() const;
		int kind() const { return params.size() + 1; }
	};

	struct type_class_t {
		type_class_t(
				identifier_t id,
				identifier_t type_var_id,
				const std::set<std::string> &superclasses,
				const types::type_t::map &overloads) :
			id(id),
			type_var_id(type_var_id),
			superclasses(superclasses),
			overloads(overloads) {}

		location_t get_location() const;
		std::string str() const;

		identifier_t const id;
		identifier_t const type_var_id;
		std::set<std::string> const superclasses;
		types::type_t::map const overloads;
	};

	struct instance_t {
		instance_t(
				identifier_t type_class_id,
				types::type_t::ref type,
				const std::vector<decl_t *> &decls) :
			type_class_id(type_class_id),
			type(type),
			decls(decls)
		{}
		std::string str() const;
		location_t get_location() const;

		identifier_t const type_class_id;
		types::type_t::ref const type;
		std::vector<decl_t *> const decls;
	};

	struct module_t {
		module_t(
				std::string name,
			   	const std::vector<decl_t *> &decls,
			   	const std::vector<type_decl_t> &type_decls,
			   	const std::vector<type_class_t *> &type_classes,
			   	const std::vector<instance_t *> &instances) :
		   	name(name),
		   	decls(decls),
		   	type_decls(type_decls),
		   	type_classes(type_classes), instances(instances)
		{}

		std::string const name;
		std::vector<decl_t *> const decls;
		std::vector<type_decl_t> const type_decls;
		std::vector<type_class_t *> const type_classes;
		std::vector<instance_t *> const instances;
	};

	struct program_t {
		program_t(
				const std::vector<decl_t *> &decls,
				const std::vector<type_class_t *> &type_classes,
				const std::vector<instance_t *> &instances,
				expr_t *expr) :
			decls(decls),
			type_classes(type_classes),
			instances(instances),
		   	expr(expr) {}

		std::vector<decl_t *> const decls;
		std::vector<type_class_t *> const type_classes;
		std::vector<instance_t *> const instances;
		expr_t * const expr;
	};
}

std::ostream &operator <<(std::ostream &os, bitter::program_t *program);
std::ostream &operator <<(std::ostream &os, bitter::decl_t *decl);
std::ostream &operator <<(std::ostream &os, bitter::expr_t *expr);
