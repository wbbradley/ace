#pragma once
#include "zion.h"
#include "dbg.h"
#include "status.h"
#include "utils.h"
#include <string>
#include <map>
#include "ast_decls.h"
#include <unordered_map>
#include "bound_type.h"
#include "var.h"
#include "signature.h"

struct bound_var_t : public var_t {
	bound_var_t() = delete;
	bound_var_t(
			location location_created,
			atom name,
			bound_type_t::ref type,
			llvm::Value *llvm_value,
			ptr<const ast::item> node)
		: location_created(location_created), name(name), type(type), llvm_value(llvm_value), node(node)
   	{
		assert(name.size() != 0);
		assert(llvm_value != nullptr);
	}
	virtual ~bound_var_t() throw() {}

	location location_created;
	atom const name;
	bound_type_t::ref const type;
	llvm::Value * const llvm_value;
	ptr<const ast::item> const node;

	std::string str() const;

	bool is_int() const;
	types::signature get_signature() const;

	typedef ptr<const bound_var_t> ref;
	typedef ptr<const bound_var_t> ref;
	typedef std::vector<ref> refs;
	typedef std::map<types::signature, ref> overloads;
	typedef std::weak_ptr<bound_var_t> weak_ref;
	typedef std::map<atom, overloads> map;

	virtual types::term::ref get_term() const;
	virtual location get_location() const;

	static ref create(
			location location_created,
			atom name,
			bound_type_t::ref type,
			llvm::Value *llvm_value,
			ptr<const ast::item> node)
	{
		return make_ptr<bound_var_t>(location_created, name, type, llvm_value, node);
	}

	static std::string str(const refs &coll) {
		std::stringstream ss;
		const char *sep = "";
		ss << "{";
		for (auto &overload : coll) {
			ss << sep << overload->str();
			sep = ", ";
		}

		ss << "}";
		return ss.str();
	}
};

struct bound_module_t : public bound_var_t {
	typedef ptr<bound_module_t> ref;

	ptr<module_scope_t> module_scope;

	bound_module_t(
			location location_created,
			atom name,
			ptr<const ast::item> node,
			ptr<module_scope_t> module_scope);

	static ref create(
			location location_created,
			atom name,
			ptr<const ast::item> node,
			ptr<module_scope_t> module_scope)
	{
		return make_ptr<bound_module_t>(location_created, name, node, module_scope);
	}

};

std::string str(const bound_var_t::refs &arguments);
std::string str(const bound_var_t::overloads &arguments);
std::ostream &operator <<(std::ostream &os, const bound_var_t &var);
types::term::ref get_args_term(bound_var_t::refs args);
bound_type_t::refs get_bound_types(bound_var_t::refs values);
std::vector<llvm::Value *> get_llvm_values(const bound_var_t::refs &vars);
