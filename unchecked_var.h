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
			atom name,
			ptr<const ast::item> node,
			ptr<module_scope_t> module_scope)
		: name(name), node(node), module_scope(module_scope)
	{
		assert(!!name);
		assert(node != nullptr);
	}
	virtual ~unchecked_var_t() throw() {}

	atom name;
	ptr<const ast::item> node;
	ptr<module_scope_t> module_scope;

	std::string str() const;

	typedef ptr<const unchecked_var_t> ref;
	typedef std::vector<ref> refs;
	typedef refs overload_vector;
	typedef std::map<atom, overload_vector> map;

	static ref create(atom name, ptr<const ast::item> node, ptr<module_scope_t> module_scope) {
		return ref(new unchecked_var_t(name, node, module_scope));
	}

	virtual types::term::ref get_term() const;
	virtual location get_location() const;
};

struct unchecked_data_ctor_t : public unchecked_var_t {
	unchecked_data_ctor_t() = delete;
	unchecked_data_ctor_t(atom name, 
			ptr<const ast::item> node,
			ptr<module_scope_t> module_scope,
			types::term::ref sig) : unchecked_var_t(name, node, module_scope), sig(sig) {}

	static ref create(
			atom name,
		   	ptr<const ast::item> node,
		   	ptr<module_scope_t> module_scope,
		   	types::term::ref sig)
   	{
		return ref(new unchecked_data_ctor_t(name, node, module_scope, sig));
	}

	virtual types::term::ref get_term() const { return sig; }

	types::term::ref sig;
};
