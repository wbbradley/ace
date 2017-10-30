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
		types::type_t::ref type,
		location_t location,
		llvm::Type *llvm_type,
		llvm::Type *llvm_specific_type) :
	type(type),
	location(location),
	llvm_type(llvm_type),
	llvm_specific_type(llvm_specific_type)
{
	assert(llvm_type != nullptr);
	assert(type->ftv_count() == 0 && "bound types should not contain type variables");

	debug_above(6, log(log_info, "creating type %s with (%s, LLVM TypeID %d, %s)",
			type->str().c_str(),
			llvm_print(llvm_specific_type).c_str(),
			llvm_type ? llvm_type->getTypeID() : -1,
			location.str().c_str()));
}

types::type_t::ref bound_type_t::get_type() const {
	return type;
}

bool bound_type_t::is_concrete() const {
	assert(type->ftv_count() == 0);
	return !is_type_id(type, {BUILTIN_UNREACHABLE_TYPE});
}

location_t const bound_type_t::get_location() const {
	return location;
}

llvm::Type *bound_type_t::get_llvm_type() const {
	return llvm_type;
}

llvm::Type *bound_type_t::get_llvm_specific_type() const {
	return llvm_specific_type ? llvm_specific_type : llvm_type;
}

bound_type_t::ref bound_type_t::get_pointer() const {
	return create(
			type_ptr(type),
			location,
			llvm_type->getPointerTo(),
			llvm_specific_type ? llvm_specific_type->getPointerTo() : nullptr);
}

bound_type_t::ref bound_type_t::create(
		types::type_t::ref type,
		location_t location,
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
		ss << sep << arg->get_type()->str();
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
// #ifdef DEBUG_LLVM_TYPES
	ss << " " << llvm_print(get_llvm_specific_type());
// #endif
	return ss.str();
}

types::type_args_t::ref get_args_type(bound_type_t::named_pairs args) {
	types::type_t::refs sig_args;
	for (auto &named_pair : args) {
		sig_args.push_back(named_pair.second->get_type());
	}
	return type_args(sig_args);
}

types::type_args_t::ref get_args_type(bound_type_t::refs args) {
	types::type_t::refs sig_args;
	for (auto &arg : args) {
		assert(arg != nullptr);
		sig_args.push_back(arg->get_type());
	}
	return type_args(sig_args);
}

types::type_args_t::ref get_args_type(bound_var_t::refs args) {
	types::type_t::refs sig_args;
	for (auto &arg : args) {
		assert(arg != nullptr);
		sig_args.push_back(arg->get_type());
	}
	return type_args(sig_args);
}

types::type_t::refs get_types(const bound_type_t::refs &bound_types) {
	types::type_t::refs types;
	for (auto &bound_type : bound_types) {
		assert(bound_type != nullptr);
		types.push_back(bound_type->get_type());
	}
	return types;
}

types::type_t::ref get_tuple_type(const bound_type_t::refs &items_types) {
	types::type_t::refs dimensions;
	types::name_index_t name_index;
	int i = 0;
	for (auto &arg : items_types) {
		assert(arg != nullptr);
		dimensions.push_back(arg->get_type());
		name_index[string_format("_%d", i)] = i;
		++i;
	}
	return type_struct(dimensions, name_index);
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

bool bound_type_t::is_ref() const {
	return get_type()->is_ref();
}

bool bound_type_t::is_function() const {
	return get_type()->is_function();
}

bool bound_type_t::is_void() const {
	return get_type()->is_void();
}

bool bound_type_t::is_maybe() const {
	if (auto maybe = dyncast<const types::type_maybe_t>(get_type())) {
		return true;
	} else {
		return false;
	}
}

bool bound_type_t::is_module() const {
	return types::is_type_id(get_type(), "module");
}

bool bound_type_t::is_ptr(scope_t::ref scope) const {
	bool res = types::is_ptr(type, scope->get_typename_env());
	debug_above(7, log("checking whether %s is a ptr of some kind: %s",
				type->str().c_str(),
				res ? c_good("it is") : c_error("it isn't")));

	assert_implies(res, llvm::dyn_cast<llvm::PointerType>(get_llvm_specific_type()));
	return res;
}

bool bound_type_t::is_opaque() const {
	if (auto llvm_struct_type = llvm::dyn_cast<llvm::StructType>(get_llvm_specific_type())) {
		return llvm_struct_type->isOpaque();
	} else {
		return false;
	}
}

void bound_type_t::is_managed_ptr(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	ptr<scope_t> scope,
		bool &is_managed) const
{
	is_managed = types::is_managed_ptr(type, scope->get_typename_env());

	if (!!status) {
		debug_above(7, log("checking whether %s is a managed ptr: %s",
					type->str().c_str(),
					is_managed ? c_good("it is") : c_error("it isn't")));

		auto program_scope = scope->get_program_scope();

		/* get the memory management structure type */
		auto var = program_scope->get_runtime_type(status, builder, "var_t");
		assert(var != nullptr);

		if (is_managed) {
			auto llvm_type = get_llvm_specific_type();
			if (is_ref()) {
				llvm::PointerType *llvm_pointer_type = llvm::dyn_cast<llvm::PointerType>(llvm_type);
				if (llvm_pointer_type != nullptr) {
					llvm_type = llvm_pointer_type->getElementType();
				} else {
					assert(false);
				}
			}

			/* sanity check that the LLVM types are sane with regards to the scope we're
			 * looking in for the typename environment */
			if (llvm::PointerType *llvm_pointer_type = llvm::dyn_cast<llvm::PointerType>(llvm_type)) {
				if (llvm::StructType *llvm_struct_type = llvm::dyn_cast<llvm::StructType>(llvm_pointer_type->getElementType())) {
					/* either this type is an unspecified managed pointer (which would
					 * need runtime type information to decipher, or it's a concrete
					 * static managed type (or not). */
					if (var->get_llvm_type() != llvm_struct_type) {
						auto &elems = llvm_struct_type->elements();
						assert_implies(is_managed, elems.size() == 2);
						if (elems.size() != 2 || var->get_llvm_specific_type() != elems[0]) {
							std::cerr << llvm_print_type(var->get_llvm_type()) << " != " << llvm_print_type(llvm_struct_type) << std::endl;
							dbg();
						}
					}
				} else {
					debug_above(1, log("%s is not a struct", llvm_print(llvm_pointer_type->getElementType()).c_str()));
					dbg();
				}
			} else {
				debug_above(1, log("%s is not a pointer", llvm_print(llvm_type).c_str()));
				assert(false);
			}
		}
	}
}


types::signature bound_type_t::get_signature() const {
	return get_type()->get_signature();
}

types::type_function_t::ref get_function_type(
		types::type_t::ref type_fn_context,
		bound_type_t::named_pairs named_args,
		bound_type_t::ref ret)
{
	bound_type_t::refs args;
	for (auto named_arg : named_args) {
		args.push_back(named_arg.second);
	}
	return get_function_type(type_fn_context, args, ret);
}

types::type_function_t::ref get_function_type(
		types::type_t::ref type_fn_context,
		bound_type_t::refs args,
		bound_type_t::ref return_type)
{
	types::type_t::refs type_args;

	for (auto arg : args) {
		type_args.push_back(arg->get_type());
	}

	return ::type_function(
			type_fn_context,
			::type_args(type_args),
			return_type->get_type());
}
