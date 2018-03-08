#pragma once
#include "zion.h"
#include "dbg.h"
#include "status.h"
#include "utils.h"
#include <string>
#include <map>
#include "ast_decls.h"
#include "bound_type.h"
#include "var.h"
#include "signature.h"

struct bound_var_t : public std::enable_shared_from_this<bound_var_t>, public var_t {
	bound_var_t() = delete;
	bound_var_t(
			location_t internal_location,
			std::string name,
			bound_type_t::ref type,
			llvm::Value *llvm_value,
			identifier::ref id) :
	   	internal_location(internal_location),
	   	name(name),
	   	type(type),
	   	id(id),
	   	llvm_value(llvm_value)
   	{
		assert(name.size() != 0);
		assert(llvm_value != nullptr);
		assert(id != nullptr);
		assert(type != nullptr);
	}

	virtual ~bound_var_t() throw() {}

	location_t internal_location;
	std::string const name;
	bound_type_t::ref const type;
	identifier::ref const id;

private:
	llvm::Value * const llvm_value;

public:
	llvm::Value *get_llvm_value() const;
	std::string str() const;

	types::signature get_signature() const;

	typedef ptr<const bound_var_t> ref;
	typedef std::vector<ref> refs;
	typedef std::map<types::signature, ref> overloads;
	typedef std::weak_ptr<bound_var_t> weak_ref;
	typedef std::map<std::string, overloads> map;

	types::type_t::ref get_type() const;
	virtual location_t get_location() const;
    virtual std::string get_name() const;

public:
	llvm::Value *resolve_bound_var_value(ptr<scope_t> scope, llvm::IRBuilder<> &builder) const;
	ref resolve_bound_value(llvm::IRBuilder<> &builder, ptr<scope_t> scope) const;

	static ref create(
			location_t internal_location,
			std::string name,
			bound_type_t::ref type,
			llvm::Value *llvm_value,
			identifier::ref id);

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

private:
   	virtual types::type_t::ref get_type(ptr<scope_t> scope) const;
};

struct bound_module_t : public bound_var_t {
	typedef ptr<bound_module_t> ref;

	ptr<module_scope_t> module_scope;

	bound_module_t(
			location_t internal_location,
			std::string name,
			identifier::ref id,
			ptr<module_scope_t> module_scope);

	static ref create(
			location_t internal_location,
			std::string name,
			identifier::ref id,
			ptr<module_scope_t> module_scope)
	{
		return make_ptr<bound_module_t>(internal_location, name, id, module_scope);
	}

};

std::string str(const bound_var_t::refs &arguments);
std::string str(const bound_var_t::overloads &arguments);
std::ostream &operator <<(std::ostream &os, const bound_var_t &var);
types::type_args_t::ref get_args_type(bound_var_t::refs args);
bound_type_t::refs get_bound_types(bound_var_t::refs values);
bound_var_t::ref resolve_alloca(llvm::IRBuilder<> &builder, bound_var_t::ref var);
