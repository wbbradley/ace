#include "zion.h"
#include "dbg.h"
#include "types.h"
#include <sstream>
#include "utils.h"
#include "types.h"
#include "parser.h"

const atom PK_OBJ = {"obj"};
const atom PK_FUNCTION = {"fn"};
const atom PK_ARGS = {"args"};
const atom PK_TUPLE = {"and"};
const atom PK_STRUCT = {"struct"};

namespace types {

	atom term::str() const {
		std::stringstream ss;
		emit(ss);
		return {ss.str()};
	}

	namespace terms {
		const char *UNREACHABLE = "void";

		struct term_unreachable : public term {
			term_unreachable() {}

			std::ostream &emit(std::ostream &os) const {
				os << UNREACHABLE;
				return os;
			}
		};

		struct term_id : public term {
			term_id(identifier::ref name) : name(name) {}
			identifier::ref name;

			std::ostream &emit(std::ostream &os) const {
				os << name;
				return os;
			}
		};

		struct term_lambda : public term {
			term_lambda(identifier::ref var, term::ref body) : var(var), body(body) {}
			identifier::ref var;
			term::ref body;

			std::ostream &emit(std::ostream &os) const {
				os << "(" << var << " " << body << ")";
				return os;
			}
		};

		struct term_sum : public term {
			term_sum(term::refs options) : options(options) {}
			term::refs options;

			std::ostream &emit(std::ostream &os) const {
				os << "(or";
				for (auto &option : options) {
					os << " " << option;
				}
				os << ")";
				return os;
			}
		};

		struct term_product : public term {
			term_product(atom kind, term::refs dimensions) : kind(kind), dimensions(dimensions) {}
			atom kind;
			term::refs dimensions;

			std::ostream &emit(std::ostream &os) const {
				os << "(" << kind;
				for (auto &dimension : dimensions) {
					os << " " << dimension;
				}
				os << ")";
				return os;
			}
		};

		int next_generic = 1;

		identifier::ref _next_term_variable() {
			/* generate fresh "any" variables */
			return make_iid({string_format("__%d", next_generic++)});
		}

		struct term_generic : public term {
			term_generic(identifier::ref name) : name(name) {}
			term_generic() : name(_next_term_variable()) {}
			identifier::ref name;

			std::ostream &emit(std::ostream &os) const {
				os << "(any " << name << ")";
				return os;
			}
		};

		struct term_apply : public term {
			term_apply(term::ref fn, term::ref arg) : fn(fn), arg(arg) {}
			term::ref fn;
			term::ref arg;

			std::ostream &emit(std::ostream &os) const {
				os << "(" << fn << " " << arg << ")";
				return os;
			}
		};

		struct term_let : public term {
			term_let(identifier::ref var, term::ref defn, term::ref body) : var(var), defn(defn), body(body) {}
			identifier::ref var;
			term::ref defn;
			term::ref body;

			std::ostream &emit(std::ostream &os) const {
				os << "(let " << var << " " << defn << " ";
				os << body << ")";
				return os;
			}
		};

		struct term_ref : public term {
			term_ref(term::ref macro, term::refs args) : macro(macro), args(args) {}
			term::ref macro;
			term::refs args;

			std::ostream &emit(std::ostream &os) const {
				os << "(ref " << macro;
				for (auto &arg : args) {
					os << " " << arg;
				}
				os << ")";
				return os;
			}
		};
	}

#if 0
	bool term_t::get_obj_struct_index(atom dim_name, int &index) const {
		/* lookup the index of a particular name in this term, if it is a
		 * struct. returns true or false for success. */
		if (is_obj()) {
			if (args[0].is_struct()) {
				auto &struct_args = args[0].args;
				for (int i = struct_args.size() - 1; i >= 0; --i) {
					if (dim_name == struct_args[i].name) {
						index = i;
						return true;
					}
				}
			}
		}
		return false;
	}
#endif

	bool term::is_generic(types::term::map env) const {
		not_impl();
		return false;
	}

	bool term::is_function(types::term::map env) const {
		not_impl();
		return false;
	}

	bool term::is_void(types::term::map env) const {
		not_impl();
		return false;
	}

	bool term::is_obj(types::term::map env) const {
		not_impl();
		return false;
	}

	bool term::is_struct(types::term::map env) const {
		not_impl();
		return false;
	}

	term::ref term_unreachable() {
		return make_ptr<terms::term_unreachable>();
	}

	term::ref term_id(identifier::ref name) {
		return make_ptr<terms::term_id>(name);
	}

	term::ref term_lambda(identifier::ref var, term::ref body) {
		return make_ptr<terms::term_lambda>(var, body);
	}

	term::ref term_sum(term::refs options) {
		return make_ptr<terms::term_sum>(options);
	}

	term::ref term_product(atom kind, term::refs dimensions) {
		return make_ptr<terms::term_product>(kind, dimensions);
	}

	term::ref term_generic(identifier::ref name) {
		return make_ptr<terms::term_generic>(name);
	}

	term::ref term_apply(term::ref fn, term::ref arg) {
		return make_ptr<terms::term_apply>(fn, arg);
	}

	term::ref term_let(identifier::ref var, term::ref defn, term::ref body) {
		return make_ptr<terms::term_let>(var, defn, body);
	}

	term::ref term_ref(term::ref macro, term::refs args) {
		return make_ptr<terms::term_ref>(macro, args);
	}

	namespace inner {
		struct type_variable : public type {
			type_variable(atom name) : name(name) {}
			atom name;
		};
	}
}

types::identifier::ref make_iid(atom name) {
	return make_ptr<types::iid>(name);
}

bool get_obj_struct_name_info(
		types::type::ref type,
	   	std::string member_name,
	   	int &index,
	   	types::type::ref &member_type)
{
	not_impl();
	// types::term::ref expr_sig = lhs_val->type->term.get_obj_struct_item_type(index);
	return false;
}

std::ostream& operator <<(std::ostream &os, const types::type::ref &type) {
	assert(false);
	return os;
}

std::ostream& operator <<(std::ostream &os, const ptr<types::term> &term) {
	if (term != nullptr) {
		return term->emit(os);
	} else {
		return os << "(error: null term)";
	}
}

types::term::ref get_args_term(types::term::refs args) {
	/* for now just use a tuple for the args */
	return types::term_product(PK_ARGS, args);
}

types::term::ref get_function_term(types::term::ref args, types::term::ref return_type) {
	return types::term_product(PK_FUNCTION, {args, return_type});
}

types::term::ref get_function_return_type_term(types::term::ref function_type) {
	return null_impl();
}

types::term::ref get_obj_term(types::term::ref item) {
	return types::term_product(PK_OBJ, {item});
}

std::ostream &operator <<(std::ostream &os, types::identifier::ref id) {
	return os << id->get_name();
}

types::term::pair make_term_pair(std::string fst, std::string snd) {
	return types::term::pair{parse_type_expr(fst), parse_type_expr(snd)};
}

types::term::ref parse_type_expr(std::string input) {
	status_t status;
	std::istringstream iss(input);
	zion_lexer_t lexer("", iss);
	parse_state_t ps(status, "", lexer, nullptr);
	types::term::ref term = null_impl(); // parse_term(ps, psc_type_ref);
	if (!!status) {
		return term;
	} else {
		panic("bad term");
		return null_impl();
	}
}

types::term::ref operator "" _ty(const char *value, size_t) {
	return parse_type_expr(value);
}

bool get_type_variable_name(types::type::ref type, atom &name) {
    if (auto ptv = dyncast<const types::inner::type_variable>(type)) {
		name = ptv->name;
		return true;
	} else {
		return false;
	}
	return false;
}
