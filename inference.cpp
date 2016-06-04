#include "dbg.h"
#include "inference.h"
#include <sstream>

namespace inference {
	term::ref term::type_ident(atom name) {
		return make_ptr<inference::type_ident>(name);
	}

	term::ref term::variable_ident(atom name) {
		return make_ptr<inference::variable_ident>(name);
	}

	term::ref term::lambda(atom var, ref body) {
		return make_ptr<inference::lambda>(var, body);
	}

	term::ref term::apply(term::ref fn, ref arg) {
		return make_ptr<inference::apply>(fn, arg);
	}

	term::ref term::let(atom var, term::ref defn, term::ref body) {
		return make_ptr<inference::let>(var, defn, body);
	}

	term::ref term::let_rec(atom var, term::ref defn, term::ref body) {
		return make_ptr<inference::let_rec>(var, defn, body);
	}

	atom term::str() const {
		std::stringstream ss;
		emit(ss);
		return {ss.str()};
	}

	std::ostream &variable_ident::emit(std::ostream &os) const {
		os << name;
		return os;
	}

	std::ostream &type_ident::emit(std::ostream &os) const {
		os << name;
		return os;
	}

	std::ostream &lambda::emit(std::ostream &os) const {
		os << "λ" << var << ".(" << body << ")";
		return os;
	}

	std::ostream &callsite::emit(std::ostream &os) const {
		os << "(" << fn << " [";
		const char *sep = "";
		for (auto &arg : args) {
			os << sep << arg;
			sep = " ";
		}
		os << "])";
		return os;
	}

	std::ostream &apply::emit(std::ostream &os) const {
		os << "(" << fn << " " << arg << ")";
		return os;
	}

	std::ostream &let::emit(std::ostream &os) const {
		os << "let " << var << " = " << defn << " in " << body;
		return os;
	}

	std::ostream &let_rec::emit(std::ostream &os) const {
		os << "let-rec " << var << " = " << defn << " in " << body;
		return os;
	}
}

std::ostream& operator <<(std::ostream &out, const ptr<inference::term> &term) {
	if (term != nullptr) {
		return term->emit(out);
	} else {
		return out << "(error: null term)";
	}
}
