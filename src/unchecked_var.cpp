#include "zion.h"
#include "dbg.h"
#include "unchecked_var.h"
#include "ast.h"
#include "bound_type.h"

unchecked_var_t::unchecked_var_t(
			identifier::ref id,
			std::shared_ptr<const ast::item_t> node,
			std::shared_ptr<module_scope_t> module_scope) :
   	id(id),
   	node(node),
   	module_scope(module_scope)
{
	assert(id->get_location() == node->get_location());
	assert(id && id->get_name().size());
	assert(node != nullptr);
}

std::string unchecked_var_t::str() const {
    std::stringstream ss;
    ss << "unchecked var : " << id->str() << " " << get_location();
    return ss.str();
}

std::string unchecked_data_ctor_t::str() const {
    std::stringstream ss;
    ss << "unchecked data ctor : " << C_ID << id->str() << C_RESET << " : ";
	ss << sig->str();
    return ss.str();
}

types::type_t::ref unchecked_data_ctor_t::get_type(scope_t::ref scope) const {
	return scope != nullptr ? sig->eval(scope) : sig;
}

types::type_t::ref unchecked_var_t::get_type() const {
	assert(false);
	return nullptr;
}

types::type_t::ref unchecked_var_t::get_type(scope_t::ref scope) const {
	if (auto fn = dyncast<const ast::function_defn_t>(node)) {
		auto decl = fn->decl;
		assert(decl != nullptr);
		if (scope != nullptr) {
			return decl->function_type->rebind(scope->get_type_variable_bindings());
		} else {
			return decl->function_type;
		}
	} else if (auto link_fn = dyncast<const ast::link_function_statement_t>(node)) {
		auto decl = link_fn->extern_function;
		assert(decl != nullptr);
		if (scope != nullptr) {
			return decl->function_type->rebind(scope->get_type_variable_bindings());
		} else {
			return decl->function_type;
		}
	} else {
		dbg();
		log_location(log_error, get_location(), "not-impl: get a type from unchecked_var %s", node->str().c_str());
		not_impl();
		return type_bottom();
	}
}

location_t unchecked_var_t::get_location() const {
	return node->token.location;
}

std::string unchecked_var_t::get_name() const {
    return id->get_name();
}

identifier::ref unchecked_var_t::get_id() const {
	return id;
}
