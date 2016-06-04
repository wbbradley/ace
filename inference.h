#pragma once
#include "zion.h"

namespace inference {

	/* terms are terms of the lambda calculus as refined by Hindley-Damas-Milner */
	struct term {
		typedef ptr<const term> ref;

		virtual ~term() {}
		virtual std::ostream &emit(std::ostream &os) const = 0;
		static ref variable_ident(atom name);
		static ref type_ident(atom name);
		static ref lambda(atom var, ref body);
		static ref apply(ref fn, ref arg);
		static ref let(atom var, ref defn, ref body);
		static ref let_rec(atom var, ref defn, ref body);

		atom str() const;
		atom repr() const;
	};

	struct type_ident : public term {
		type_ident(atom name) : name(name) {}
		virtual std::ostream &emit(std::ostream &os) const;
		atom name;
	};

	struct variable_ident : public term {
		variable_ident(atom name) : name(name) {}
		virtual std::ostream &emit(std::ostream &os) const;
		atom name;
	};

	struct lambda : public term {
		lambda(atom var, ref body) : var(var), body(body) {}
		virtual std::ostream &emit(std::ostream &os) const;
		atom var;
		ref body;
	};

	struct apply : public term {
		apply(ref fn, ref arg) : fn(fn), arg(arg) {}
		virtual std::ostream &emit(std::ostream &os) const;
		ref fn;
		ref arg;
	};

	struct callsite : public term {
		callsite(ref fn, std::vector<ref> args);
		virtual std::ostream &emit(std::ostream &os) const;
		ref fn;
		std::vector<ref> args;
	};

	struct let : public term {
		let(atom var, ref defn, ref body) : var(var), defn(defn), body(body) {}
		virtual std::ostream &emit(std::ostream &os) const;
		atom var;
		ref defn;
		ref body;
	};

	struct let_rec : public term {
		let_rec(atom var, ref defn, ref body) : var(var), defn(defn), body(body) {}
		virtual std::ostream &emit(std::ostream &os) const;
		atom var;
		ref defn;
		ref body;
	};
};

std::ostream& operator <<(std::ostream &out, const inference::term::ref &term);
