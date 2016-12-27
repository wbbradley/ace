#include "zion.h"
#include "dbg.h"
#include "types.h"
#include <sstream>
#include "utils.h"
#include "types.h"
#include "parser.h"
#include "type_visitor.h"

const char *BUILTIN_LIST_TYPE = "std.List";

int next_generic = 1;

void reset_generics() {
	next_generic = 1;
}

namespace types {

#if 0
	term::ref change_product_kind(product_kind_t pk, term::ref product) {
		auto term_product = dyncast<const struct terms::term_product>(product);
		if (term_product != nullptr) {
			if (term_product->pk == pk) {
				return term_product;
			} else {
				return types::term_product(pk, term_product->dimensions);
			}
		} else {
			panic("i thought this would be a product term!");
		}
		return null_impl();
	}
#endif

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

	bool type_id::accept(type_visitor &visitor) const {
		return visitor.visit(*this);
	}

	type::ref type_id::rebind(const map &bindings) const {
		return shared_from_this();
	}

	location type_id::get_location() const {
		return id->get_location();
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

	bool type_variable::accept(type_visitor &visitor) const {
		return visitor.visit(*this);
	}

	location type_variable::get_location() const {
		return id->get_location();
	}

	type_operator::type_operator(type::ref oper, type::ref operand) :
		oper(oper), operand(operand)
	{
	}

	std::ostream &type_operator::emit(std::ostream &os, const map &bindings) const {
		oper->emit(os, bindings);
		os << "{";
		operand->emit(os, bindings);
		return os << "}";
	}

	int type_operator::ftv() const {
		return oper->ftv() + operand->ftv();
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
		return ftv_sum;
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

types::type::ref type_operator(types::type::ref operator_, types::type::ref operand) {
	return make_ptr<types::type_operator>(operator_, operand);
}

types::type::ref type_product(product_kind_t pk, types::type::refs dimensions) {
	return make_ptr<types::type_product>(pk, dimensions);
}

types::type::ref type_sum(types::type::refs options) {
	return make_ptr<types::type_sum>(options);
}

std::ostream& operator <<(std::ostream &os, const types::type::ref &type) {
	os << type->str();
	return os;
}

types::type::ref get_args_type(types::type::refs args) {
	/* for now just use a tuple for the args */
	return type_product(pk_args, args);
}

types::type::ref get_function_type(types::type::ref args, types::type::ref return_type) {
	return types::type_product(pk_function, {args, return_type});
}

types::type::refs get_function_type_args(types::type::ref function_type) {
	debug_above(5, log(log_info, "getting function type_args from %s", function_type->str().c_str()));

	auto type_product = dyncast<const types::type_product>(function_type);
	assert(type_product != nullptr);
	assert(type_product->pk == pk_function);
	assert(type_product->dimensions.size() == 2);

	auto type_args = dyncast<const types::type_product>(type_product->dimensions[0]);
	assert(type_args != nullptr);
	assert(type_args->pk == pk_args);
	return type_args->dimensions;
}

types::type::ref get_function_return_type(types::type::ref function_type) {
	debug_above(5, log(log_info, "getting function return type from %s", function_type->str().c_str()));

	auto type_product = dyncast<const types::type_product>(function_type);
	if (type_product == nullptr) {
		dbg();
	}
	assert(type_product->pk == pk_function);
	assert(type_product->dimensions.size() == 2);

	return type_product->dimensions[1];
}

types::type::ref get_function_type_args(types::type::ref function_type) {
	debug_above(5, log(log_info, "sig == %s", function_type->str().c_str()));

	auto type_product = dyncast<const types::type_product>(function_type);
	assert(type_product != nullptr);
	assert(type_product->pk == pk_function);
	assert(type_product->dimensions.size() == 2);

	auto type_args = dyncast<const struct types::type_product>(type_product->dimensions[0]);
	return type_args;
}

types::type::ref get_obj_type(types::type::ref item) {
	return type_product(pk_obj, {item});
}

std::ostream &operator <<(std::ostream &os, identifier::ref id) {
	return os << id->get_name();
}

types::type::pair make_type_pair(std::string fst, std::string snd, identifier::set generics) {
	debug_above(4, log(log_info, "creating type pair with (%s, %s) and generics [%s]",
				fst.c_str(), snd.c_str(),
			   	join(generics, ", ").c_str()));

	return types::type::pair{parse_type_expr(fst, generics), parse_type_expr(snd, generics)};
}

types::type::ref parse_type_expr(std::string input, identifier::set generics) {
	status_t status;
	std::istringstream iss(input);
	zion_lexer_t lexer("", iss);
	parse_state_t ps(status, "", lexer, nullptr);
	types::type::ref type = parse_type(ps, generics);
	if (!!status) {
		return type;
	} else {
		panic("bad type");
		return null_impl();
	}
}

types::type::ref operator "" _ty(const char *value, size_t) {
	return parse_type_expr(value, {});
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

std::string str(types::type::refs refs) {
	std::stringstream ss;
	ss << "(";
	const char *sep = "";
	for (auto p : refs) {
		ss << sep << p->str();
		sep = ", ";
	}
	ss << ")";
	return ss.str();
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

std::string str(types::type::map coll) {
	std::stringstream ss;
	ss << "{";
	const char *sep = "";
	for (auto p : coll) {
		ss << sep << C_ID << p.first.c_str() << C_RESET << ": ";
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
