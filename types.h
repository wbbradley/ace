#pragma once
#include "zion.h"
#include "ast_decls.h"

/* Product Kinds */
extern const atom PK_OBJ;
extern const atom PK_FUNCTION;
extern const atom PK_ARGS;
extern const atom PK_TUPLE;
extern const atom PK_STRUCT;

namespace types {

	/* the abstract notion of an identifer */
	struct identifier {
		typedef ptr<const identifier> ref;

		virtual ~identifier() {}
		virtual atom get_name() const = 0;
	};

	/* internal identifiers - note that they lack a "location" */
	struct iid : public identifier {
		typedef ptr<iid> ref;

		atom name;

		iid(atom name) : name(name) {}
		iid() = delete;
		iid(const iid &) = delete;
		iid(const iid &&) = delete;
		iid &operator =(const iid &) = delete;
		
		virtual atom get_name() const {
			return name;
		}
	};

	struct term;

	struct type {
		typedef ptr<const type> ref;
		typedef std::vector<ref> refs;
		typedef std::map<atom, ref> map;

		virtual ~type() {}
		virtual std::ostream &emit(std::ostream &os) const = 0;

		/* how many free type variables exist in this type? */
		virtual int ftv() const = 0;

		virtual atom str(const map &bindings) const = 0;
		virtual ptr<const term> to_term(const map &bindings={}) const = 0;

		atom repr() const { return this->str({}); }
		atom str() const { return this->str({}); }
	};

	struct type_variable;

	/* type data ctors */
	type::ref type_unreachable();
	type::ref type_id(identifier::ref var);
	type::ref type_variable(identifier::ref name);
	type::ref type_ref(type::ref macro, type::refs args);
	type::ref type_operator(type::ref operator_, type::ref operand);
	type::ref type_sum(type::refs options);
	type::ref type_product(type::refs dimensions);

	bool is_type_id(type::ref type, atom type_name);

	/* term is the base-type of terms as terms of the
	 * lambda calculus as refined by
	 * Hindley-Damas-Milner. It also includes the
	 * addition of the polymorph type used in Zion to
	 * unify sum types. */
	struct term {
		typedef ptr<const term> ref;
		typedef std::vector<ref> refs;
		typedef std::map<identifier::ref, ref> map;

		virtual ~term() {}

		virtual std::ostream &emit(std::ostream &os) const = 0;

		virtual ref evaluate(map env, int macro_depth) const;
		virtual type::ref get_type() const;

		atom repr() const;
		atom str() const;

		bool is_generic(types::term::map env) const;
		bool is_function(types::term::map env) const;
		bool is_void(types::term::map env) const;
		bool is_obj(types::term::map env) const;
		bool is_struct(types::term::map env) const;
	};

	/* term data ctors */
	term::ref term_unreachable();
	term::ref term_id(identifier::ref name);
	term::ref term_lambda(identifier::ref var, term::ref body);
	term::ref term_sum(term::refs options);
	term::ref term_product(atom kind, term::refs dimensions);
	term::ref term_generic(identifier::ref name);
	term::ref term_generic();
	term::ref term_apply(term::ref fn, term::ref arg);
	term::ref term_let(identifier::ref var, term::ref defn, term::ref body);
	term::ref term_ref(term::ref macro, term::refs args);
};

namespace std {
	template <>
	struct hash<types::identifier::ref> {
		int operator ()(const types::identifier::ref &s) const {
			return 1301081 * s->get_name().iatom;
		}
	};
}

std::ostream &operator <<(std::ostream &os, types::identifier::ref id);
std::string str(types::term::refs refs);
std::string str(types::term::map coll);
std::ostream& operator <<(std::ostream &out, const types::term::ref &term);

/* helper functions */
types::identifier::ref make_iid(atom name);
types::term::ref get_args_term(types::term::refs args);
types::term::ref get_function_term(types::term::ref args, types::term::ref return_type);
bool get_obj_struct_name_info(types::type::ref type, std::string member_name, int &index, types::type::ref &member_type);
