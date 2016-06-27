#include "zion.h"
#include "dbg.h"
#include "bound_type.h"
#include "scopes.h"
#include "bound_var.h"
#include "ast.h"

bound_type_t::bound_type_t(
		types::type::ref type,
		struct location location,
		llvm::Type *llvm_type) :
	type(type),
	location(location),
	llvm_type(llvm_type)
{
	debug_above(4, log(log_info, "creating type with (%s, LLVM TypeID %d, %s)",
			type->str().c_str(),
			llvm_type ? llvm_type->getTypeID() : -1,
			location.str().c_str()));

	assert(llvm_type != nullptr);
	assert(type->ftv() == 0);
}

bound_type_t::ref bound_type_t::create(
		types::type::ref type,
		struct location location,
		llvm::Type *llvm_type)
{
	return make_ptr<bound_type_t>(type, location, llvm_type);
}

types::term::ref bound_type_t::get_term() const {
	assert(type->ftv() == 0);
	return type->to_term();
}

#if 0
std::string bound_type_t::str(const map &coll) {
	std::stringstream ss;
	const char *sep = "";
	ss << "{";
	for (auto &pair : coll) {
		ss << sep << C_TYPE << pair.first.str() << C_RESET << ": ";
		ss << pair.second->str();
	}
	ss << "}";
	return ss.str();
}
#endif

std::string str(const bound_type_t::refs &args) {
	std::stringstream ss;
	ss << "[";
	const char *sep = "";
	for (auto &arg : args) {
		ss << sep << arg->str();
		sep = ", ";
	}
	ss << "]";
	return ss.str();
}

std::string str(const bound_type_t::named_pairs &named_pairs) {
	std::stringstream ss;
	ss << "[";
	const char *sep = "";
	for (auto &pair : named_pairs) {
		ss << sep << "(" << pair.first << " " << pair.second->str() << ")";
		sep = ", ";
	}
	ss << "]";
	return ss.str();
}

std::ostream &operator <<(std::ostream &os, const bound_type_t &type) {
	return os << type.str();
}

std::string bound_type_t::str() const {
	std::stringstream ss;
	ss << type;
	return ss.str();
}

types::term::ref get_args_term(bound_type_t::named_pairs args) {
	types::term::refs sig_args;
	for (auto &named_pair : args) {
		sig_args.push_back(named_pair.second->type->to_term());
	}
	return get_args_term(sig_args);
}

types::term::ref get_args_term(bound_type_t::refs args) {
	types::term::refs sig_args;
	for (auto &arg : args) {
		assert(arg != nullptr);
		sig_args.push_back(arg->type->to_term());
	}
	return get_args_term(sig_args);
}

types::term::ref get_args_term(bound_var_t::refs args) {
	types::term::refs sig_args;
	for (auto &arg : args) {
		assert(arg != nullptr);
		sig_args.push_back(arg->get_term());
	}
	return get_args_term(sig_args);
}

types::term::refs get_terms(const bound_type_t::refs &bound_types) {
	types::term::refs terms;
	for (auto &bound_type : bound_types) {
		assert(bound_type != nullptr);
		terms.push_back(bound_type->get_term());
	}
	return terms;
}

types::term::ref get_tuple_term(const bound_type_t::refs &items_types) {
	types::term::refs dimensions;
	for (auto &arg : items_types) {
		assert(arg != nullptr);
		dimensions.push_back(arg->type->to_term());
	}
	return get_tuple_term(dimensions);
}

types::term::ref get_tuple_term(types::term::refs dimensions) {
	return types::term_product(pk_tuple, dimensions);
}

bound_type_t::refs bound_type_t::refs_from_vars(const bound_var_t::refs &args) {
	bound_type_t::refs arg_types;
	for (auto &arg : args) {
		assert(arg != nullptr);
		assert(arg->type != nullptr);
		arg_types.push_back(arg->type);
	}
	return arg_types;
}

bool bound_type_t::is_function() const {
	return type->is_function();
}

bool bound_type_t::is_void() const {
	return type->is_void();
}

bool bound_type_t::is_obj() const {
	return type->is_obj();
}

bool bound_type_t::is_struct() const {
	return type->is_struct();
}

types::signature bound_type_t::get_signature() const {
	return type->get_signature();
}

types::term::ref get_function_term(
		bound_type_t::refs args,
		bound_type_t::ref return_value)
{
	types::term::refs arg_terms;
	for (auto arg : args) {
		arg_terms.push_back(arg->type->to_term());
	}
	types::term::ref args_term = get_args_term(arg_terms);
	return get_function_term(args_term, return_value->type->to_term());
}

types::term::ref get_function_term(
		bound_type_t::named_pairs named_args,
		bound_type_t::ref ret)
{
	bound_type_t::refs args;
	for (auto named_arg : named_args) {
		args.push_back(named_arg.second);
	}
	return get_function_term(args, ret);
}

types::type::ref get_function_type(
		bound_type_t::refs args,
		bound_type_t::ref return_type)
{
	types::type::refs type_args;

	for (auto arg : args) {
		type_args.push_back(arg->type);
	}

	return ::type_product(pk_function,
			{::type_product(pk_args, type_args), return_type->type});
}
