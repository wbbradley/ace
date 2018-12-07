#include "delegate.h"
#include "llvm_types.h"
#include "checked_var.h"
#include "llvm_utils.h"

llvm::IRBuilder<> &delegate_t::get_builder(location_t location) {
	if (!use_llvm) {
		throw user_error(location, "not allowed here");
	}
	return builder;
}

delegate_t delegate_t::get_type_delegate() const {
	return {builder, false};
}

bound_type_t::ref delegate_t::upsert_bound_type(
		scope_t::ref scope,
	   	types::type_t::ref type)
{
	return ::upsert_bound_type(builder, scope, type);
}

var_t::ref delegate_t::refine_var_type(
		scope_t::ref scope,
		location_t internal_location,
		types::type_t::ref refined_type,
		var_t::ref value,
		identifier::ref id)
{
	bound_type_t::ref bound_refined_type = upsert_bound_type(scope, refined_type);
	if (auto bound_var = dyncast<const bound_var_t>(value)) {
		return make_bound_var(
				internal_location,
				id->get_name(),
				bound_refined_type,
				bound_var->get_llvm_value(scope),
				id);
	} else {
		return make_checked_var(refined_type, id);
	}
}

var_t::ref delegate_t::instantiate_unchecked_fn(
		scope_t::ref scope,
		unchecked_var_t::ref unchecked_fn,
		types::type_function_t::ref fn_type,
		const types::type_t::map &bindings)
{
	return ::instantiate_unchecked_fn(builder, scope, unchecked_fn, fn_type, bindings);
}

var_t::ref delegate_t::dereferencing_load(
		var_t::ref value,
		scope_t::ref scope)
{
	if (auto bound_var = dyncast<const bound_var_t>(value)) {
		return bound_var->dereferencing_load(builder, scope);
	} else {
		if (auto ref_type = dyncast<const types::type_ref_t>(value->get_type())) {
			return make_checked_var(ref_type->element_type, value->get_id());
		}
		return value;
	}
}

var_t::ref delegate_t::create_callsite(
        scope_t::ref scope,
		life_t::ref life,
		var_t::ref function_,
		std::string name,
		const location_t &location,
		var_t::refs arguments_)
{
	if (use_llvm) {
		bound_var_t::ref function = safe_dyncast<const bound_var_t>(function_);
		bound_var_t::refs arguments;
		for (auto arg : arguments_) {
			arguments.push_back(safe_dyncast<const bound_var_t>(arg));
		}
		return ::create_callsite(builder, scope, life, function, name, location, arguments);
	} else {
		types::type_t::ref return_type = types::without_closure(types::without_ref(function_->get_type()))->return_type;
		debug_above(5, log("delegate_t::create_callsite -> %s", return_type->str().c_str()));
		return make_checked_var(return_type, make_iid_impl(name, location));
	}
}

std::function<void ()> delegate_t::get_ip_restorer() {
	if (use_llvm) {
		auto saved_ip = builder.saveIP();
		return [this, saved_ip] () {
			builder.restoreIP(saved_ip);
		};
	} else {
		return [] () {};
	}
}

bound_type_t::ref delegate_t::get_bound_type(scope_t::ref scope, var_t::ref val) {
	if (auto bound_var = dyncast<const bound_var_t>(val)) {
		return bound_var->get_bound_type();
	} else {
		return upsert_bound_type(scope, val->get_type(scope));
	}
}
