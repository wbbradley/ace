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
			ptr<const ast::item> node,
			ptr<module_scope_t> module_scope)
		: id(id), node(node), module_scope(module_scope)
	{
		assert(id && !!id->get_name());
		assert(node != nullptr);
	}
	virtual ~unchecked_var_t() throw() {}

	identifier::ref id;
	ptr<const ast::item> node;
	ptr<module_scope_t> module_scope;

	std::string str() const;

	typedef ptr<const unchecked_var_t> ref;
	typedef std::vector<ref> refs;
	typedef refs overload_vector;
	typedef std::map<atom, overload_vector> map;

	static ref create(
			identifier::ref id,
		   	ptr<const ast::item> node,
		   	ptr<module_scope_t> module_scope)
	{
		return ref(new unchecked_var_t(id, node, module_scope));
	}

    virtual types::type::ref get_type() const;
	virtual location get_location() const;
};

struct unchecked_data_ctor_t : public unchecked_var_t {
	unchecked_data_ctor_t() = delete;
	unchecked_data_ctor_t(
			identifier::ref id,
			ptr<const ast::item> node,
			ptr<module_scope_t> module_scope,
			types::type::ref sig,
			atom::map<int> member_index) :
	   	unchecked_var_t(id, node, module_scope),
	   	sig(sig),
		member_index(member_index) {}

	static ref create(
			identifier::ref id,
		   	ptr<const ast::item> node,
		   	ptr<module_scope_t> module_scope,
		   	types::type::ref sig,
			atom::map<int> member_index)
   	{
		return ref(new unchecked_data_ctor_t(id, node, module_scope, sig, member_index));
	}

    virtual types::type::ref get_type() const;

	types::type::ref sig;
	atom::map<int> member_index;
};
