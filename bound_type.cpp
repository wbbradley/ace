#include "zion.h"
#include "dbg.h"
#include "bound_type.h"
#include "scopes.h"
#include "bound_var.h"
#include "ast.h"
#include "type_instantiation.h"
#include "llvm_types.h"
#include "llvm_utils.h"
#include <iostream>

bound_type_t::bound_type_t(
		types::type::ref type,
		struct location location,
		llvm::Type *llvm_type,
		llvm::Type *llvm_specific_type) :
	type(type),
	location(location),
	llvm_type(llvm_type),
	llvm_specific_type(llvm_specific_type)
{
	debug_above(6, log(log_info, "creating type %s with (%s, LLVM TypeID %d, %s)",
			type->str().c_str(),
			llvm_print_type(llvm_specific_type).c_str(),
			llvm_type ? llvm_type->getTypeID() : -1,
			location.str().c_str()));

	assert(type->ftv_count() == 0 && "bound types should not contain type variables");
	assert(llvm_type != nullptr);
}

types::type::ref bound_type_t::get_type() const {
	return type;
}

bool bound_type_t::is_concrete() const {
	assert(type->ftv_count() == 0);
	return !is_type_id(type, {BUILTIN_UNREACHABLE_TYPE});
}

struct location const bound_type_t::get_location() const {
	return location;
}

llvm::Type * const bound_type_t::get_llvm_type() const {
	return llvm_type;
}

llvm::Type * const bound_type_t::get_llvm_specific_type() const {
	return llvm_specific_type ? llvm_specific_type : llvm_type;
}

/*
bound_type_t::refs const bound_type_t::get_dimensions() const {
	return type->get_dimensions();
}

bound_type_t::name_index const bound_type_t::get_member_index() const {
	return type->get_name_index();
}
*/

bound_type_t::ref bound_type_t::create(
		types::type::ref type,
		struct location location,
		llvm::Type *llvm_type,
		llvm::Type *llvm_specific_type)
{
	return make_ptr<bound_type_t>(type, location, llvm_type,
			llvm_specific_type ? llvm_specific_type : llvm_type);
}

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

std::string str(const bound_type_t::name_index &name_index) {
	std::stringstream ss;
	ss << "{";
	const char *sep = "";
	for (auto &pair : name_index) {
		ss << sep << pair.first << ": " << pair.second;
		sep = ", ";
	}
	ss << "}";
	return ss.str();
}

std::ostream &operator <<(std::ostream &os, const bound_type_t &type) {
	return os << type.str();
}

std::string bound_type_t::str() const {
	std::stringstream ss;
	ss << get_type();
	ss << " " << llvm_print_type(get_llvm_specific_type());
	return ss.str();
}

types::type_args::ref get_args_type(bound_type_t::named_pairs args) {
	types::type::refs sig_args;
	for (auto &named_pair : args) {
		sig_args.push_back(named_pair.second->get_type());
	}
	return type_args(sig_args);
}

types::type_args::ref get_args_type(bound_type_t::refs args) {
	types::type::refs sig_args;
	for (auto &arg : args) {
		assert(arg != nullptr);
		sig_args.push_back(arg->get_type());
	}
	return type_args(sig_args);
}

types::type_args::ref get_args_type(bound_var_t::refs args) {
	types::type::refs sig_args;
	for (auto &arg : args) {
		assert(arg != nullptr);
		sig_args.push_back(arg->get_type());
	}
	return type_args(sig_args);
}

types::type::refs get_types(const bound_type_t::refs &bound_types) {
	types::type::refs types;
	for (auto &bound_type : bound_types) {
		assert(bound_type != nullptr);
		types.push_back(bound_type->get_type());
	}
	return types;
}

types::type::ref get_tuple_type(const bound_type_t::refs &items_types, bool managed) {
	types::type::refs dimensions;
	types::name_index name_index;
	int i = 0;
	for (auto &arg : items_types) {
		assert(arg != nullptr);
		dimensions.push_back(arg->get_type());
		name_index[string_format("_%d", i)] = i;
		++i;
	}
	return type_struct(dimensions, name_index, managed);
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
	return get_type()->is_function();
}

bool bound_type_t::is_void() const {
	return get_type()->is_void();
}

bool bound_type_t::is_maybe() const {
	if (auto maybe = dyncast<const types::type_maybe>(get_type())) {
		return true;
	} else {
		return false;
	}
}

bool bound_type_t::is_ref() const {
	if (auto product = dyncast<const types::type_ref>(get_type())) {
		return true;
	} else {
		return false;
	}
}

types::signature bound_type_t::get_signature() const {
	return get_type()->get_signature();
}

types::type_function::ref get_function_type(
		types::type::ref type_fn_context,
		bound_type_t::named_pairs named_args,
		bound_type_t::ref ret)
{
	bound_type_t::refs args;
	for (auto named_arg : named_args) {
		args.push_back(named_arg.second);
	}
	return get_function_type(type_fn_context, args, ret);
}

types::type_function::ref get_function_type(
		types::type::ref type_fn_context,
		bound_type_t::refs args,
		bound_type_t::ref return_type)
{
	types::type::refs type_args;

	for (auto arg : args) {
		type_args.push_back(arg->get_type());
	}

	return ::type_function(
			type_fn_context,
			::type_args(type_args),
			return_type->get_type());
}
