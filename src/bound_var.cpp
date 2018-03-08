#include "bound_var.h"
#include "llvm_utils.h"
#include "llvm_types.h"
#include "ast.h"
#include "parser.h"
#include "scopes.h"

bound_var_t::ref bound_var_t::create(
		location_t internal_location,
		std::string name,
		bound_type_t::ref type,
		llvm::Value *llvm_value,
		identifier::ref id)
{
	assert(type != nullptr);
#ifdef ZION_DEBUG
	if (type->get_type()->eval_predicate(tb_ref, {}, {})) {
		assert(llvm::dyn_cast<llvm::AllocaInst>(llvm_value) || llvm_value->getType()->isPointerTy());
	}
#endif

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

std::string bound_var_t::get_name() const {
	return name;
}

llvm::Value *bound_var_t::get_llvm_value() const {
	return llvm_value;
}


llvm::Value *bound_var_t::resolve_bound_var_value(scope_t::ref scope, llvm::IRBuilder<> &builder) const {
	if (type->get_type()->eval_predicate(tb_ref, scope)) {
		return builder.CreateLoad(llvm_value);
	} else {
		assert(!llvm::dyn_cast<llvm::GlobalVariable>(llvm_value));
	}

	return llvm_value;
}

bound_var_t::ref bound_var_t::resolve_bound_value(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope) const
{
	if (auto ref_type = dyncast<const types::type_ref_t>(type->get_type())) {
		auto bound_type = upsert_bound_type(builder, scope, ref_type->element_type);
		return bound_var_t::create(
				INTERNAL_LOC(),
				this->name,
				bound_type,
				resolve_bound_var_value(scope, builder),
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
		std::string name,
		identifier::ref id,
		module_scope_t::ref module_scope) :
	bound_var_t(internal_location,
			name, 
			module_scope->get_bound_type({"module"}),
			llvm::Constant::getNullValue(module_scope->get_program_scope()->get_bound_type("null")->get_llvm_type()),
			id),
	module_scope(module_scope)
{
	assert(module_scope != nullptr);
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
