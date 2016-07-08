#include "zion.h"
#include "dbg.h"
#include "types.h"
#include <sstream>
#include "utils.h"
#include "types.h"
#include "parser.h"
#include "type_visitor.h"

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
				return shared_from_this();
			}

			type::ref get_type() const {
				return ::type_id(make_iid({"void"}));
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
				auto iter = env.find(name->get_name());
				if (iter == env.end()) {
					return shared_from_this();
				} else {
					auto value = iter->second;
					if (value != shared_from_this()) {
						return value->evaluate(env, macro_depth);
					} else {
						return value;
					}
				}
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
				term::refs evaluated_options;
				for (auto &option : options) {
					evaluated_options.push_back(option->evaluate(env, macro_depth));
				}
				return types::term_sum(evaluated_options);
			}

			virtual type::ref get_type() const {
				type::refs type_options;
				for (auto &option : options) {
					type_options.push_back(option->get_type());
				}
				return ::type_sum(type_options);
			}
		};

		struct term_product : public term {
			term_product(product_kind_t pk, term::refs dimensions) : pk(pk), dimensions(dimensions) {}
			virtual ~term_product() {}

			product_kind_t pk;
			term::refs dimensions;

			virtual std::ostream &emit(std::ostream &os) const {
				os << "(" << pkstr(pk);
				for (auto &dimension : dimensions) {
					os << " " << dimension;
				}
				os << ")";
				return os;
			}

			virtual ref evaluate(map env, int macro_depth) const {
				term::refs evaluated_dimensions;
				
				for (auto &dimension : dimensions) {
					evaluated_dimensions.push_back(dimension->evaluate(env, macro_depth));
				}
				return types::term_product(pk, evaluated_dimensions);
			}

			virtual type::ref get_type() const {
				type::refs type_dimensions;
				for (auto dimension : dimensions) {
					type_dimensions.push_back(dimension->get_type());
				}
				return ::type_product(pk, type_dimensions);
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

	std::string term::str() const {
		return string_format(c_type("%s"), repr().c_str());
	}

	bool term::is_generic(types::term::map env) const {
		auto type = evaluate(env, 0)->get_type();
		return type->ftv() != 0;
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

	term::ref term_product(product_kind_t pk, term::refs dimensions) {
		return make_ptr<terms::term_product>(pk, dimensions);
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

	/**********************************************************************/
	/* Types                                                              */
	/**********************************************************************/

	std::string type::str(const map &bindings) const {
	   	return string_format(c_type("%s"), this->repr(bindings).c_str());
   	}

	atom type::repr(const map &bindings) const {
		std::stringstream ss;
		emit(ss, bindings);
		return ss.str();
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

	ptr<const term> type_id::to_term(const map &bindings) const {
		if (id->get_name() == "void") {
			return term_unreachable();
		} else {
			return term_id(id);
		}
	}

	bool type_id::accept(type_visitor &visitor) const {
		return visitor.visit(*this);
	}

	type::ref type_id::rebind(const map &bindings) const {
		return shared_from_this();
	}

	location type_id::get_location() const {
		return INTERNAL_LOC();
	}

	bool type_id::is_void() const {
	   	return id->get_name() == atom{"void"};
   	}

	type_variable::type_variable(identifier::ref id) : id(id) {
	}

	std::ostream &type_variable::emit(std::ostream &os, const map &bindings) const {
		auto instance_iter = bindings.find(id->get_name());
		if (instance_iter != bindings.end()) {
			return instance_iter->second->emit(os, bindings);
		} else {
			return os << string_format("(any %s)", id->get_name().c_str());
		}
	}

	/* how many free type variables exist in this type? */
	int type_variable::ftv() const {
		return 1;
	}

	type::ref type_variable::rebind(const map &bindings) const {
		auto instance_iter = bindings.find(id->get_name());
		if (instance_iter != bindings.end()) {
			return instance_iter->second->rebind(bindings);
		} else {
			return shared_from_this();
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

	bool type_variable::accept(type_visitor &visitor) const {
		return visitor.visit(*this);
	}

	location type_variable::get_location() const {
		auto loc = id->get_location();
		if (loc != nullptr) {
			return *loc;
		} else {
			return INTERNAL_LOC();
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

	ptr<const term> type_ref::to_term(const map &bindings) const {
		term::refs term_args;
		for (auto arg : args) {
			term_args.push_back(arg->to_term(bindings));
		}
		return term_ref(macro->to_term(bindings), term_args);
	}

	bool type_ref::accept(type_visitor &visitor) const {
		return visitor.visit(*this);
	}

	type::ref type_ref::rebind(const map &bindings) const {
		refs type_args;
		for (auto arg : args) {
			type_args.push_back(arg->rebind(bindings));
		}
		/* probably no need to rebind the macro */
		return ::type_ref(macro->rebind(bindings), type_args);
	}

	location type_ref::get_location() const {
		return macro->get_location();
	}

	type_operator::type_operator(type::ref oper, type::ref operand) :
		oper(oper), operand(operand)
	{
	}

	std::ostream &type_operator::emit(std::ostream &os, const map &bindings) const {
		os << "(";
		oper->emit(os, bindings);
		os << " ";
		operand->emit(os, bindings);
		return os << ")";
	}

	int type_operator::ftv() const {
		return oper->ftv() + operand->ftv();
	}

	ptr<const term> type_operator::to_term(const map &bindings) const {
		return term_apply(oper->to_term(bindings), operand->to_term(bindings));
	}

	bool type_operator::accept(type_visitor &visitor) const {
		return visitor.visit(*this);
	}

	type::ref type_operator::rebind(const map &bindings) const {
		return ::type_operator(oper->rebind(bindings), operand->rebind(bindings));
	}

	location type_operator::get_location() const {
		return oper->get_location();
	}

	type_product::type_product(product_kind_t pk, type::refs dimensions) :
		pk(pk), dimensions(dimensions)
	{
	}

	std::ostream &type_product::emit(std::ostream &os, const map &bindings) const {
		os << "(" << pkstr(pk);
		for (auto dimension : dimensions) {
			os << " ";
			dimension->emit(os, bindings);
		}
		return os << ")";
	}

	int type_product::ftv() const {
		int ftv_sum = 0;
		for (auto dimension : dimensions) {
			ftv_sum += dimension->ftv();
		}
		return ftv_sum;
	}

	ptr<const term> type_product::to_term(const map &bindings) const {
		term::refs term_dimensions;
		for (auto dimension : dimensions) {
			term_dimensions.push_back(dimension->to_term(bindings));
		}
		return term_product(pk, term_dimensions);
	}

	bool type_product::accept(type_visitor &visitor) const {
		return visitor.visit(*this);
	}

	type::ref type_product::rebind(const map &bindings) const {
		refs type_dimensions;
		for (auto dimension : dimensions) {
			type_dimensions.push_back(dimension->rebind(bindings));
		}
		return ::type_product(pk, type_dimensions);
	}

	location type_product::get_location() const {
		if (dimensions.size() != 0) {
			return dimensions[0]->get_location();
		} else {
			return INTERNAL_LOC();
		}
	}

	bool type_product::is_function() const {
	   	return pk == pk_function;
   	}

	bool type_product::is_obj() const {
	   	return pk == pk_obj;
   	}

	bool type_product::is_struct() const {
	   	return pk == pk_struct;
   	}

	type_sum::type_sum(type::refs options) :
		options(options)
	{
	}

	std::ostream &type_sum::emit(std::ostream &os, const map &bindings) const {
		os << "(or";
		assert(options.size() != 0);
		for (auto option : options) {
			os << " ";
			option->emit(os, bindings);
		}
		return os << ")";
	}

	int type_sum::ftv() const {
		int ftv_sum = 0;
		for (auto option : options) {
			ftv_sum += option->ftv();
		}
		if (ftv_sum != 0) {
			assert(!"generics should probably have been converted to unreachables....");
		}
		return ftv_sum;
	}

	ptr<const term> type_sum::to_term(const map &bindings) const {
		term::refs term_options;
		for (auto option : options) {
			term_options.push_back(option->to_term(bindings));
		}
		return term_sum(term_options);
	}

	bool type_sum::accept(type_visitor &visitor) const {
		return visitor.visit(*this);
	}

	type::ref type_sum::rebind(const map &bindings) const {
		refs type_options;
		for (auto option : options) {
			type_options.push_back(option->rebind(bindings));
		}
		return ::type_sum(type_options);
	}

	location type_sum::get_location() const {
		if (options.size() != 0) {
			return options[0]->get_location();
		} else {
			return INTERNAL_LOC();
		}
	}

	bool is_type_id(type::ref type, atom type_name) {
		if (auto pti = dyncast<const types::type_id>(type)) {
			return pti->id->get_name() == type_name;
		}
		return false;
	}
}

types::type::ref type_id(identifier::ref id) {
	return make_ptr<types::type_id>(id);
}

types::type::ref type_variable(identifier::ref id) {
	return make_ptr<types::type_variable>(id);
}

types::type::ref type_ref(types::type::ref macro, types::type::refs args) {
	return make_ptr<types::type_ref>(macro, args);
}

types::type::ref type_operator(types::type::ref operator_, types::type::ref operand) {
	return make_ptr<types::type_operator>(operator_, operand);
}

types::type::ref type_product(product_kind_t pk, types::type::refs dimensions) {
	return make_ptr<types::type_product>(pk, dimensions);
}

types::type::ref type_sum(types::type::refs options) {
	return make_ptr<types::type_sum>(options);
}

identifier::ref make_iid(atom name) {
	return make_ptr<iid>(name);
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
	os << type->str();
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
	return types::term_product(pk_args, args);
}

types::term::ref get_function_term(types::term::ref args, types::term::ref return_type) {
	return types::term_product(pk_function, {args, return_type});
}

types::term::ref get_function_return_type_term(types::term::ref function_type) {
	return null_impl();
}

types::term::ref get_obj_term(types::term::ref item) {
	return types::term_product(pk_obj, {item});
}

std::ostream &operator <<(std::ostream &os, identifier::ref id) {
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

std::string str(types::term::map coll) {
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

const char *pkstr(product_kind_t pk) {
	switch (pk) {
	case pk_obj:
		return "obj";
	case pk_function:
		return "fn";
	case pk_args:
		return "args";
	case pk_tuple:
		return "and";
	case pk_tag:
		return "tag";
	case pk_tagged_tuple:
		return "tagged-tuple";
	case pk_struct:
		return "struct";
	case pk_named_dimension:
		return "dim";
	}
	assert(false);
	return nullptr;
}
