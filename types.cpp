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

	namespace terms {
		const char *UNREACHABLE = "void";

		struct term_unreachable : public term {
			term_unreachable() {}
			virtual ~term_unreachable() {}

			std::ostream &emit(std::ostream &os) const {
				os << UNREACHABLE;
				return os;
			}

			ref evaluate(map env, int macro_depth) const {
				return null_impl();
			}

			type::ref get_type() const {
				return null_impl();
			}
		};

		struct term_id : public term {
			term_id(identifier::ref name) : name(name) {}
			virtual ~term_id() {}
			identifier::ref name;

			std::ostream &emit(std::ostream &os) const {
				os << name;
				return os;
			}

			ref evaluate(map env, int macro_depth) const {
				return shared_from_this();
			}

			type::ref get_type() const {
				return ::type_id(name);
			}
		};

		struct term_lambda : public term {
			term_lambda(identifier::ref var, term::ref body) : var(var), body(body) {}
			virtual ~term_lambda() {}
			identifier::ref var;
			term::ref body;

			std::ostream &emit(std::ostream &os) const {
				os << "(" << var << " " << body << ")";
				return os;
			}

			ref evaluate(map env, int macro_depth) const {
				return null_impl();
			}

			type::ref get_type() const {
				return null_impl();
			}
		};

		struct term_sum : public term {
			term_sum(term::refs options) : options(options) {}
			~term_sum() {}
			term::refs options;

			virtual std::ostream &emit(std::ostream &os) const {
				os << "(or";
				for (auto &option : options) {
					os << " " << option;
				}
				os << ")";
				return os;
			}

			virtual ref evaluate(map env, int macro_depth) const {
				return null_impl();
			}

			virtual type::ref get_type() const {
				return null_impl();
			}
		};

		struct term_product : public term {
			term_product(atom kind, term::refs dimensions) : kind(kind), dimensions(dimensions) {}
			virtual ~term_product() {}

			atom kind;
			term::refs dimensions;

			virtual std::ostream &emit(std::ostream &os) const {
				os << "(" << kind;
				for (auto &dimension : dimensions) {
					os << " " << dimension;
				}
				os << ")";
				return os;
			}

			virtual ref evaluate(map env, int macro_depth) const {
				return null_impl();
			}

			virtual type::ref get_type() const {
				return null_impl();
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

			ref evaluate(map env, int macro_depth) const {
				/* Only allow substitution of "any" type variables from the environment. */
				return shared_from_this();
			}

			type::ref get_type() const {
				return ::type_variable(name);
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

			ref evaluate(map env, int macro_depth) const {
				debug_above(5, log(log_info, "evaluating term_apply %s", str().c_str()));
				auto fn_eval = fn->evaluate(env, macro_depth);
				auto arg_eval = arg->evaluate(env, macro_depth);

				if (auto pfn = dyncast<const types::terms::term_lambda>(fn_eval)) {
					/* We should only handle substitutions in lambdas when they
					 * are being applied. */
					env[pfn->var->get_name()] = arg_eval;
					return pfn->body->evaluate(env, macro_depth);
				} else {
					return shared_from_this();
				}
			}

			type::ref get_type() const {
				return ::type_operator(fn->get_type(),
						arg->get_type());
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

			ref evaluate(map env, int macro_depth) const {
				return null_impl();
			}

			type::ref get_type() const {
				return null_impl();
			}
		};

		struct term_ref : public term {
			term_ref(term::ref macro, term::refs args) : macro(macro), args(args) {}
			term::ref macro;
			term::refs args;

			virtual ~term_ref() {}

			std::ostream &emit(std::ostream &os) const {
				os << "(ref " << macro;
				for (auto &arg : args) {
					os << " " << arg;
				}
				os << ")";
				return os;
			}

			ref evaluate(map env, int macro_depth) const {
				return null_impl();
			}

			type::ref get_type() const {
				return null_impl();
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

	atom term::repr() const {
		std::stringstream ss;
		emit(ss);
		return {ss.str()};
	}

	atom term::str() const {
		return string_format(c_type("%s"), repr().c_str());
	}

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

	term::ref term_generic() {
		return make_ptr<terms::term_generic>();
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

	type_id::type_id(identifier::ref id) : id(id) {
	}

	std::ostream &type_id::emit(std::ostream &os, const map &bindings) const {
		return os << id->get_name();
	}

	int type_id::ftv() const {
		/* how many free type variables exist in this type? */
		return 0;
	}

	atom type_id::str(const map &bindings) const {
		return {id->get_name()};
	}

	ptr<const term> type_id::to_term(const map &bindings) const {
		return term_id(id);
	}

	type_variable::type_variable(identifier::ref id) : id(id) {
	}

	std::ostream &type_variable::emit(std::ostream &os, const map &bindings) const {
		return os << str(bindings);
	}

	/* how many free type variables exist in this type? */
	int type_variable::ftv() const {
		return 1;
	}

	atom type_variable::str(const map &bindings) const {
		auto instance_iter = bindings.find(id->get_name());
		if (instance_iter != bindings.end()) {
			return instance_iter->second->str(bindings);
		} else {
			return string_format("(any %s)", id->get_name().c_str());
		}
	}

	ptr<const term> type_variable::to_term(const map &bindings) const {
		auto instance_iter = bindings.find(id->get_name());
		if (instance_iter != bindings.end()) {
			return instance_iter->second->to_term(bindings);
		} else {
			return term_generic(id);
		}
	}

	type_ref::type_ref(type::ref macro, type::refs args) :
		macro(macro), args(args)
	{
	}

	std::ostream &type_ref::emit(std::ostream &os, const map &bindings) const {
		os << "(ref ";
		macro->emit(os, bindings);
		for (auto arg : args) {
			os << " ";
			arg->emit(os, bindings);
		}
		return os << ")";
	}

	int type_ref::ftv() const {
		return 0;
	}

	atom type_ref::str(const map &bindings) const {
		std::stringstream ss;
		emit(ss, bindings);
		return ss.str();
	}

	ptr<const term> type_ref::to_term(const map &bindings) const {
		term::refs term_args;
		for (auto arg : args) {
			term_args.push_back(arg->to_term(bindings));
		}
		return term_ref(macro->to_term(bindings), term_args);
	}

	type_operator::type_operator(type::ref oper, type::ref operand) :
		oper(oper), operand(operand)
	{
	}

	std::ostream &type_operator::emit(std::ostream &os, const map &bindings) const {
		os << "(";
		oper->emit(os, bindings);
		os << " ";
		oper->emit(os, bindings);
		return os << ")";
	}

	int type_operator::ftv() const {
		return oper->ftv() + operand->ftv();
	}

	atom type_operator::str(const map &bindings) const {
		std::stringstream ss;
		emit(ss, bindings);
		return ss.str();
	}

	ptr<const term> type_operator::to_term(const map &bindings) const {
		return term_apply(oper->to_term(bindings), operand->to_term(bindings));
	}

	bool is_type_id(type::ref type, atom type_name) {
		if (auto pti = dyncast<const types::type_id>(type)) {
			return pti->id->get_name() == type_name;
		}
		return false;
	}
}

types::type::ref type_id(types::identifier::ref id) {
	return make_ptr<types::type_id>(id);
}

types::type::ref type_variable(types::identifier::ref id) {
	return make_ptr<types::type_variable>(id);
}

types::type::ref type_ref(types::type::ref macro, types::type::refs args) {
	return make_ptr<types::type_ref>(macro, args);
}

types::type::ref type_operator(types::type::ref operator_, types::type::ref operand) {
	return make_ptr<types::type_operator>(operator_, operand);
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

std::ostream& operator <<(std::ostream &os, const types::term::ref &term) {
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
	debug_above(4, log(log_info, "creating term pair with (%s, %s)",
				fst.c_str(), snd.c_str()));

	return types::term::pair{parse_type_expr(fst), parse_type_expr(snd)};
}

types::term::ref parse_type_expr(std::string input) {
	status_t status;
	std::istringstream iss(input);
	zion_lexer_t lexer("", iss);
	parse_state_t ps(status, "", lexer, nullptr);
	types::term::ref term = parse_term(ps);
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
    if (auto ptv = dyncast<const types::type_variable>(type)) {
		name = ptv->id->get_name();
		return true;
	} else {
		return false;
	}
	return false;
}

std::string str(types::type::map coll) {
	std::stringstream ss;
	ss << "{";
	const char *sep = "";
	for (auto p : coll) {
		ss << sep << p.first.c_str() << ": ";
		ss << p.second->str().c_str();
		sep = ", ";
	}
	ss << "}";
	return ss.str();
}
