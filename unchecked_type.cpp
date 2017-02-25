#include "unchecked_type.h"
#include "scopes.h"
#include "ast.h"

std::string unchecked_type_t::str() const {
	return node->str();
}

unchecked_type_t::unchecked_type_t(
		atom name,
		ptr<const ast::item_t> node,
		ptr<scope_t> const module_scope) :
	name(name), node(node)
{
	debug_above(5, log(log_info, "creating unchecked type " c_type("%s"), this->name.c_str()));

	assert(!!this->name);
	assert(this->node != nullptr);
}

unchecked_type_t::ref unchecked_type_t::create(
		atom name,
		ptr<const ast::item_t> node,
		ptr<scope_t> const module_scope)
{
	return ref(new unchecked_type_t(name, node, module_scope));
}
