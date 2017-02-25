#pragma once
#include "zion.h"
#include "dbg.h"
#include "status.h"
#include "utils.h"
#include <string>
#include <map>
#include "ast_decls.h"
#include <unordered_map>
#include "bound_var.h"
#include "var.h"

struct unchecked_var_t : public var_t {
	unchecked_var_t() = delete;
	unchecked_var_t(
			identifier::ref id,
			ptr<const ast::item_t> node,
			ptr<module_scope_t> module_scope)
		: id(id), node(node), module_scope(module_scope)
	{
		assert(id && !!id->get_name());
		assert(node != nullptr);
	}
	virtual ~unchecked_var_t() throw() {}

	identifier::ref id;
	ptr<const ast::item_t> node;
	ptr<module_scope_t> module_scope;

	virtual std::string str() const;

	typedef ptr<const unchecked_var_t> ref;
	typedef std::vector<ref> refs;
	typedef refs overload_vector;
	typedef std::map<atom, overload_vector> map;

	static ref create(
			identifier::ref id,
		   	ptr<const ast::item_t> node,
		   	ptr<module_scope_t> module_scope)
	{
		return ref(new unchecked_var_t(id, node, module_scope));
	}

    virtual types::type_t::ref get_type(ptr<scope_t> scope) const;
	virtual location_t get_location() const;
};

struct unchecked_data_ctor_t : public unchecked_var_t {
	unchecked_data_ctor_t() = delete;
	unchecked_data_ctor_t(
			identifier::ref id,
			ptr<const ast::item_t> node,
			ptr<module_scope_t> module_scope,
			types::type_function_t::ref sig) :
	   	unchecked_var_t(id, node, module_scope),
	   	sig(sig) {}

	static ref create(
			identifier::ref id,
		   	ptr<const ast::item_t> node,
		   	ptr<module_scope_t> module_scope,
		   	types::type_function_t::ref sig)
   	{
		return ref(new unchecked_data_ctor_t(id, node, module_scope, sig));
	}

    virtual types::type_t::ref get_type(ptr<scope_t> scope) const;

	virtual std::string str() const;

	types::type_function_t::ref sig;
};
