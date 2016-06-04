#pragma once
#include "zion.h"

namespace types {

	struct type {
		typedef ptr<const type> ref;
		typedef std::vector<ref> refs;
		typedef std::map<atom, ref> map;

		virtual ~type() {}
		virtual std::ostream &emit(std::ostream &os) const = 0;

		virtual atom to_str(const map &bindings) const = 0;
	};

	/* type data ctors */
	type::ref type_id(atom var);
	type::ref type_variable(atom name);
	type::ref type_operator(atom name);
	type::ref type_sum(type::refs options);

	/* term is the base-type of terms as terms of the
	 * lambda calculus as refined by
	 * Hindley-Damas-Milner. It also includes the
	 * addition of the polymorph type used in Zion to
	 * unify sum types. */
	struct term {
		typedef ptr<const term> ref;
		typedef std::vector<ref> refs;
		typedef std::map<atom, ref> map;

		virtual ~term() {}

		virtual std::ostream &emit(std::ostream &os) const = 0;

		virtual ref evaluate(map env) const;
		virtual ref apply(map env, ref arg) const;
		virtual type::ref get_type() const;

		atom str() const;
	};

	/* term data ctors */
	static term::ref term_unit(atom name);
	static term::ref term_generic(atom name);
	static term::ref term_id(atom name);
	static term::ref term_lambda(atom var, term::ref body);
	static term::ref term_polymorph(term::refs options);
	static term::ref term_apply(term::ref fn, term::ref arg);
	static term::ref term_let(atom var, term::ref defn, term::ref body);
	static term::ref term_let_rec(atom var, term::ref defn, term::ref body);
	static term::ref term_ref(term::ref body);
};

std::ostream& operator <<(std::ostream &out, const types::term::ref &term);
std::ostream& operator <<(std::ostream &out, const types::type::ref &type);
