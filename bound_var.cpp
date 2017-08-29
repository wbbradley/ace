#include "bound_var.h"
#include "llvm_utils.h"
#include "llvm_types.h"
#include "ast.h"
#include "parser.h"

bound_var_t::ref bound_var_t::create(
		location_t internal_location,
		atom name,
		bound_type_t::ref type,
		llvm::Value *llvm_value,
		identifier::ref id)
{
	if (auto llvm_alloca = llvm::dyn_cast<llvm::AllocaInst>(llvm_value)) {
		assert(type->is_ref());
	}
	if (type->is_ref()) {
		assert(llvm::dyn_cast<llvm::AllocaInst>(llvm_value) || llvm_value->getType()->isPointerTy());
	}

	return make_ptr<bound_var_t>(internal_location, name, type, llvm_value, id);
}

std::string bound_var_t::str() const {
	std::stringstream ss;
	ss << C_VAR << name << C_RESET;
	ss << " : " << id->str();
	ss << " : " << *type;

	assert(llvm_value != nullptr);

	if (debug_level() >= 10) {
		ss << " IR: ";
		std::string llir = llvm_print(*llvm_value);
		trim(llir);
		ss << C_IR << llvm_value;
		ss << " : " << llir << C_RESET;
		ss << " " << internal_location.str();
	}
    return ss.str();
}

location_t bound_var_t::get_location() const {
	return id->get_location();
}

bool bound_var_t::is_ref() const {
	return type->is_ref();
}

bool bound_var_t::is_int() const {
	/* anything that is an integer value is a bool under the covers */
	return llvm_resolve_type(llvm_value)->isIntegerTy();
}

bool bound_var_t::is_pointer() const {
	/* anything that is an pointer */
	return llvm_resolve_type(llvm_value)->isPointerTy();
}

llvm::Value *bound_var_t::get_llvm_value() const {
	return llvm_value;
}


llvm::Value *bound_var_t::resolve_bound_var_value(llvm::IRBuilder<> &builder) const {
	if (type->is_ref()) {
		return builder.CreateLoad(llvm_value);
	} else {
		assert(!llvm::dyn_cast<llvm::AllocaInst>(llvm_value));
		assert(!llvm::dyn_cast<llvm::GlobalVariable>(llvm_value));
	}

	return llvm_value;
}

bound_var_t::ref bound_var_t::resolve_bound_value(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope) const
{
	if (auto ref_type = dyncast<const types::type_ref_t>(type->get_type())) {
		auto bound_type = upsert_bound_type(status, builder, scope, ref_type->element_type);
		return bound_var_t::create(
				INTERNAL_LOC(),
				this->name,
				bound_type,
				resolve_bound_var_value(builder),
				this->id);
	}
	return shared_from_this();
}

std::ostream &operator <<(std::ostream &os, const bound_var_t &var) {
	return os << var.str();
}

std::string str(const bound_var_t::overloads &overloads) {
	std::stringstream ss;
	const char *indent = "\t";
	for (auto &var_overload : overloads) {
		ss << indent << var_overload.first.str() << ": ";
	   	ss << var_overload.second->str() << std::endl;
	}
	return ss.str();
}

std::string str(const bound_var_t::refs &args) {
	std::stringstream ss;
	ss << "[";
	ss << join_str(args, ", ");
	ss << "]";
	return ss.str();
}

bound_module_t::bound_module_t(
		location_t internal_location,
		atom name,
		identifier::ref id,
		module_scope_t::ref module_scope) :
	bound_var_t(internal_location,
			name, 
			module_scope->get_bound_type({"module"}),
			module_scope->get_program_scope()->get_singleton("nil")->get_llvm_value(),
			id),
	module_scope(module_scope)
{
	assert(module_scope != nullptr);
}

std::vector<llvm::Value *> get_llvm_values(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		ptr<const types::type_args_t> type_args,
	   	const bound_var_t::refs &vars)
{
	std::vector<llvm::Value *> llvm_values;
	llvm_values.reserve(vars.size());

	if (type_args->args.size() != vars.size()) {
		user_error(status, location, "invalid parameter count to function call. expected %d parameters, got %d",
				(int)type_args->args.size(),
				(int)vars.size());
		return {};
	}

	auto type_iter = type_args->args.begin();
	for (auto var : vars) {
		auto &lhs_arg_type = *type_iter;
		auto rhs_arg_type = var->type->get_type();

		if (rhs_arg_type->get_signature() != lhs_arg_type->get_signature()) {
			// There is probably going to end up being some coupling here with
			// unification, since we'll need to make these compatible. Perhaps
			// unification could produce a list of instructions on how to do the
			// conversion...
			debug_above(5, log(log_info, "seeing about coercion from %s",
						llvm_print(var->get_llvm_value()).c_str()));
			debug_above(5, log(log_info, "seeing about coercion from %s to %s",
						rhs_arg_type->get_signature().c_str(),
						lhs_arg_type->get_signature().c_str()));
			debug_above(5, log(log_info, "seeing about coercion from %s to %s",
						rhs_arg_type->str().c_str(),
						lhs_arg_type->str().c_str()));

			/* check pragmatically for certain coercions that should take place */
			if (false /* the param is a native type and the target is a managed type */) {
				not_impl();
			} else if (rhs_arg_type->is_ref() && !lhs_arg_type->is_ref()) {
				llvm_values.push_back(var->resolve_bound_var_value(builder));
			} else {
				debug_above(1, log(log_info, "probably need to write some smarter coercion code"));
				llvm_values.push_back(var->resolve_bound_var_value(builder));
			}
		} else {
			llvm_values.push_back(var->get_llvm_value());
		}

		++type_iter;
	}

	return llvm_values;
}

bound_type_t::refs get_bound_types(bound_var_t::refs values) {
	bound_type_t::refs types;
	types.reserve(values.size());

	for (auto value : values) {
		types.push_back(value->type);
	}

	return types;
}

types::signature bound_var_t::get_signature() const {
	return type->get_signature();
}

types::type_t::ref bound_var_t::get_type(ptr<scope_t> scope) const {
	return type->get_type();
}

types::type_t::ref bound_var_t::get_type() const {
	return type->get_type();
}
