#pragma once
#include "zion.h"
#include "dbg.h"
#include "user_error.h"
#include "utils.h"
#include <string>
#include <map>
#include "ast_decls.h"
#include "bound_type.h"
#include "var.h"

struct closure_scope_t;

struct bound_var_t : public var_t {
	typedef ptr<const bound_var_t> ref;
	typedef std::vector<ref> refs;
	typedef std::map<std::string, ref> overloads;
	typedef std::weak_ptr<bound_var_t> weak_ref;
	typedef std::map<std::string, overloads> map;

	virtual ~bound_var_t() throw() {}
	virtual ptr<bound_var_t> this_bound_var() = 0;
	virtual ptr<const bound_var_t> this_bound_var() const = 0;
	virtual llvm::Value *get_llvm_value(ptr<scope_t> scope) const = 0;
	virtual std::string str() const = 0;
	virtual types::type_t::ref get_type() const = 0;
	virtual bound_type_t::ref get_bound_type() const = 0;
	virtual std::string get_signature() const = 0;
	virtual location_t get_location() const = 0;
    virtual std::string get_name() const = 0;
    virtual identifier::ref get_id() const = 0;
	virtual llvm::Value *resolve_bound_var_value(ptr<scope_t> scope, llvm::IRBuilder<> &builder) const = 0;
	virtual ref resolve_bound_value(llvm::IRBuilder<> &builder, ptr<scope_t> scope) const = 0;

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

bound_var_t::ref get_closure_over(
		bound_var_t::ref var,
	   	ptr<scope_t> found_in_scope,
	   	ptr<scope_t> usage_scope);

struct bound_module_t : public bound_var_t {
	virtual ptr<module_scope_t> get_module_scope() const = 0;
};

bound_var_t::ref make_bound_var(
			location_t internal_location,
			std::string name,
			bound_type_t::ref type,
			llvm::Value *llvm_value,
			identifier::ref id);
bound_var_t::ref make_lazily_bound_var(
		ptr<closure_scope_t> closure_scope,
	   	bound_var_t::ref var,
		std::function<bound_var_t::ref (ptr<scope_t>)> resolver);
bound_var_t::ref make_bound_module(
			location_t internal_location,
			std::string name,
			identifier::ref id,
			ptr<module_scope_t> module_scope);

std::string str(const bound_var_t::refs &arguments);
std::string str(const bound_var_t::overloads &arguments);
std::ostream &operator <<(std::ostream &os, const bound_var_t &var);
types::type_args_t::ref get_args_type(bound_var_t::refs args);
bound_type_t::refs get_bound_types(bound_var_t::refs values);
bound_var_t::ref resolve_alloca(llvm::IRBuilder<> &builder, bound_var_t::ref var);
