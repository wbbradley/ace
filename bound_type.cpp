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

llvm::Type * const bound_type_t::get_llvm_most_specific_type() const {
	auto llvm_type = get_llvm_specific_type();
	if (llvm_type != nullptr) {
		return llvm_type;
	} else {
		return get_llvm_type();
	}
}

bound_type_impl_t::bound_type_impl_t(
		types::type::ref type,
		struct location location,
		llvm::Type *llvm_type,
		llvm::Type *llvm_specific_type,
		bound_type_t::refs dimensions,
		name_index member_index) :
	type(type),
	location(location),
	llvm_type(llvm_type),
	llvm_specific_type(llvm_specific_type),
	dimensions(dimensions),
	member_index(member_index)
{
	debug_above(6, log(log_info, "creating type with (%s, LLVM TypeID %d, %s, %s %s)",
			type->str().c_str(),
			llvm_type ? llvm_type->getTypeID() : -1,
			location.str().c_str(),
			::str(dimensions).c_str(),
			::str(member_index).c_str()));

	assert(llvm_type != nullptr);
}

types::type::ref bound_type_impl_t::get_type() const {
	return type;
}

bool bound_type_impl_t::is_concrete() const {
	if (type->repr() != "__unreachable") {
		// NOTE: this assert may be ok to delete, since we could potentially have
		// nested types that we can't actually access without pattern matching,
		// which will check reachability
		assert(strstr(type->repr().c_str(), "__unreachable") == nullptr);
		return true;
	} else {
		return false;
	}
}

struct location const bound_type_impl_t::get_location() const {
	return location;
}

llvm::Type * const bound_type_impl_t::get_llvm_type() const {
	return llvm_type;
}

llvm::Type * const bound_type_impl_t::get_llvm_specific_type() const {
	return llvm_specific_type;
}

bound_type_t::refs const bound_type_impl_t::get_dimensions() const {
	return dimensions;
}

bound_type_t::name_index const bound_type_impl_t::get_member_index() const {
	return member_index;
}

bound_type_t::ref bound_type_t::create(
		types::type::ref type,
		struct location location,
		llvm::Type *llvm_type,
		llvm::Type *llvm_specific_type,
		bound_type_t::refs dimensions,
		bound_type_t::name_index member_index)
{
	return make_ptr<bound_type_impl_t>(type, location, llvm_type,
			llvm_specific_type, dimensions, member_index);
}

bound_type_handle_t::ref bound_type_t::create_handle(
		types::type::ref type,
		llvm::Type *llvm_type)
{
	return make_ptr<bound_type_handle_t>(type, llvm_type);
}

bound_type_handle_t::bound_type_handle_t(
		types::type::ref type,
		llvm::Type *llvm_type) :
	type(type), llvm_type(llvm_type)
{
	// std::cerr << str() << std::endl;
	// dbg();
}

std::string bound_type_handle_t::str() const {
	std::stringstream ss;
	ss << get_type();
#if 0
	ss << " " << llvm_print_type(*get_llvm_type());
	if (actual != nullptr) {
		ss << " (actual: " <<  actual->str() << ")";
	} else {
		ss << " " C_UNCHECKED "unresolved" C_RESET;
	}
#endif

	return ss.str();
}

types::type::ref bound_type_handle_t::get_type() const {
	return type;
}

bool bound_type_handle_t::is_concrete() const {
	if (actual != nullptr) {
		return actual->is_concrete();
	} else {
		assert(!"This type is not actualized yet! Why do you care about concreteness?");
		/* err on the side of not concrete if we don't have an actual type */
		return false;
	}
}

struct location const bound_type_handle_t::get_location() const {
	return type->get_location();
}

llvm::Type * const bound_type_handle_t::get_llvm_type() const {
	return llvm_type;
}

llvm::Type * const bound_type_handle_t::get_llvm_specific_type() const {
	if (actual != nullptr) {
		return actual->get_llvm_specific_type();
	} else {
		assert(false);
		return {};
	}
}

bound_type_t::refs const bound_type_handle_t::get_dimensions() const {
	if (actual != nullptr) {
		return actual->get_dimensions();
	} else {
		debug_above(9, log(log_info,
				   	"attempt to fetch dimensions on an unreachable type"));
		return {};
	}
}

bound_type_t::name_index const bound_type_handle_t::get_member_index() const {
	if (actual != nullptr) {
		return actual->get_member_index();
	} else {
		debug_above(9, log(log_info,
					"attempt to fetch member_index on an unreachable type"));
		return {};
	}
}

void bound_type_handle_t::set_actual(bound_type_t::ref new_actual) const {
	assert(new_actual != nullptr);
	assert_implies(actual != nullptr, new_actual == actual);
	assert(new_actual != shared_from_this());
	assert(new_actual->get_llvm_type() == llvm_type);
	debug_above(2, log(log_info, "resolving %s to %s", this->str().c_str(), new_actual->str().c_str()));
	// dbg();
	actual = new_actual;
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

std::string bound_type_impl_t::str() const {
	std::stringstream ss;
	ss << get_type();
	// ss << " " << llvm_print_type(*get_llvm_type());
	// ss << " " << ::str(get_dimensions());
	return ss.str();
}

types::type_product::ref get_args_type(bound_type_t::named_pairs args) {
	types::type::refs sig_args;
	for (auto &named_pair : args) {
		sig_args.push_back(named_pair.second->get_type());
	}
	return get_args_type(sig_args);
}

types::type_product::ref get_args_type(bound_type_t::refs args) {
	types::type::refs sig_args;
	for (auto &arg : args) {
		assert(arg != nullptr);
		sig_args.push_back(arg->get_type());
	}
	return get_args_type(sig_args);
}

types::type_product::ref get_args_type(bound_var_t::refs args) {
	types::type::refs sig_args;
	for (auto &arg : args) {
		assert(arg != nullptr);
		sig_args.push_back(arg->get_type());
	}
	return get_args_type(sig_args);
}

types::type::refs get_types(const bound_type_t::refs &bound_types) {
	types::type::refs types;
	for (auto &bound_type : bound_types) {
		assert(bound_type != nullptr);
		types.push_back(bound_type->get_type());
	}
	return types;
}

types::type::ref get_tuple_type(types::type::refs dimensions) {
	return type_product(pk_tuple, dimensions);
}

types::type::ref get_tuple_type(const bound_type_t::refs &items_types) {
	types::type::refs dimensions;
	for (auto &arg : items_types) {
		assert(arg != nullptr);
		dimensions.push_back(arg->get_type());
	}
	return get_tuple_type(dimensions);
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
			::type_product(pk_args, type_args),
			return_type->get_type());
}
