#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "signature.h"
#include "utils.h"
#include "identifier.h"

extern const char *BUILTIN_LIST_TYPE;

/* Product Kinds */
enum product_kind_t {
	pk_obj = 0,
	pk_function,
	pk_args,
	pk_tuple,
	pk_tag,
	pk_tagged_tuple,
	pk_struct,
	pk_named_dimension,
};

const char *pkstr(product_kind_t pk);

/* used to reset the generic type id counter */
void reset_generics();

namespace types {

	struct term;
	struct signature;
	struct type_visitor;

	struct type : public std::enable_shared_from_this<type> {
		typedef ptr<const type> ref;
		typedef std::vector<ref> refs;
		typedef std::map<atom, ref> map;

		virtual ~type() {}
		virtual std::ostream &emit(std::ostream &os, const map &bindings) const = 0;

		/* how many free type variables exist in this type? */
		virtual int ftv() const = 0;

		atom repr(const map &bindings) const;
		atom repr() const { return this->repr({}); }

		virtual ptr<const term> to_term(const map &bindings={}) const = 0;
		virtual location get_location() const = 0;

		std::string str(const map &bindings = {}) const;
		atom get_signature() const { return repr(); }

		virtual bool accept(type_visitor &visitor) const = 0;
		virtual ref rebind(const map &bindings) const = 0;

		virtual bool is_function() const { return false; }
		virtual bool is_void() const { return false; }
		virtual bool is_obj() const { return false; }
		virtual bool is_struct() const { return false; }
	};

	bool is_type_id(type::ref type, atom type_name);

	/* term is the base-type of terms as terms of the
	 * lambda calculus as refined by
	 * Hindley-Damas-Milner. It also includes the
	 * addition of the polymorph type used in Zion to
	 * unify sum types. */
	struct term : public std::enable_shared_from_this<term> {
		typedef ptr<const term> ref;
		typedef std::vector<ref> refs;
		typedef std::map<atom, ref> map;
		typedef std::pair<ref, ref> pair;

		virtual ~term() {}

		/* which type variables exist unbound in this type term? */
		virtual atom::set unbound_vars(atom::set bound_vars={}) const = 0;

		virtual ref evaluate(map env, int macro_depth) const = 0;
		virtual type::ref get_type() const = 0;
		virtual std::ostream &emit(std::ostream &os) const = 0;

		atom repr() const;
		std::string str() const;

		bool is_generic(types::term::map env) const;
	};

	term::ref change_product_kind(product_kind_t pk, term::ref product);

	/* term data ctors */
	term::ref term_unreachable();
	term::ref term_id(identifier::ref id);
	term::ref term_lambda(identifier::ref var, term::ref body);
	term::ref term_lambda_reducer(term::ref body, identifier::ref var);
	term::ref term_sum(term::refs options);
	term::ref term_product(product_kind_t pk, term::refs dimensions);
	term::ref term_generic(identifier::ref name);
	term::ref term_generic();
	term::ref term_apply(term::ref fn, term::ref arg);
	term::ref term_let(identifier::ref var, term::ref defn, term::ref body);
	term::ref term_list_type(term::ref element_term);

	struct type_id : public type {
		type_id(identifier::ref id);
		identifier::ref id;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv() const;
		virtual ptr<const term> to_term(const map &bindings={}) const;
		virtual bool accept(type_visitor &visitor) const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;
		virtual bool is_void() const;
	};

	struct type_variable : public type {
		type_variable(identifier::ref id);
		identifier::ref id;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv() const;
		virtual ptr<const term> to_term(const map &bindings={}) const;
		virtual bool accept(type_visitor &visitor) const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;
	};

	struct type_operator : public type {
		type_operator(type::ref oper, type::ref operand);
		type::ref oper;
		type::ref operand;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv() const;
		virtual ptr<const term> to_term(const map &bindings={}) const;
		virtual bool accept(type_visitor &visitor) const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;
	};

	struct type_product : public type {
		type_product(product_kind_t pk, type::refs dimensions);
		product_kind_t pk;
		type::refs dimensions;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv() const;
		virtual ptr<const term> to_term(const map &bindings={}) const;
		virtual bool accept(type_visitor &visitor) const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;

		virtual bool is_function() const;
		virtual bool is_obj() const;
		virtual bool is_struct() const;
	};

	struct type_sum : public type {
		type_sum(type::refs options);
		type::refs options;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv() const;
		virtual ptr<const term> to_term(const map &bindings={}) const;
		virtual bool accept(type_visitor &visitor) const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;

		virtual bool is_obj() const { return true; }
	};
};

/* type data ctors */
types::type::ref type_unreachable();
types::type::ref type_id(identifier::ref var);
types::type::ref type_variable(identifier::ref name);
types::type::ref type_operator(types::type::ref operator_, types::type::ref operand);
types::type::ref type_product(product_kind_t pk, types::type::refs dimensions);
types::type::ref type_sum(types::type::refs options);

namespace std {
	template <>
	struct hash<identifier::ref> {
		int operator ()(const identifier::ref &s) const {
			return 1301081 * s->get_name().iatom;
		}
	};
}

std::ostream &operator <<(std::ostream &os, identifier::ref id);
std::string str(types::term::refs refs);
std::string str(types::term::map coll);
std::string str(types::type::map coll);
std::ostream& operator <<(std::ostream &out, const types::term::ref &term);
std::ostream& operator <<(std::ostream &out, const types::type::ref &type);

/* helper functions */
types::term::ref get_args_term(types::term::refs args);
types::term::ref get_function_term(types::term::ref args, types::term::ref return_type);
types::type::refs get_function_type_args(types::type::ref function_type);
types::type::ref get_function_return_type(types::type::ref function_type);
types::term::ref get_obj_term(types::term::ref item);
types::term::ref get_function_term_args(types::term::ref function_term);
types::term::pair make_term_pair(std::string fst, std::string snd, atom::set generics);

types::term::ref operator "" _ty(const char *value, size_t);
types::term::ref parse_type_expr(std::string input, atom::set generics);
bool get_type_variable_name(types::type::ref term, atom &name);
