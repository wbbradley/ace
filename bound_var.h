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
			location_t internal_location,
			atom name,
			bound_type_t::ref type,
			llvm::Value *llvm_value,
			identifier::ref id,
			bool is_global) :
	   	internal_location(internal_location),
	   	name(name),
	   	type(type),
	   	llvm_value(llvm_value),
	   	id(id),
		_is_global(is_global)
   	{
		assert(name.size() != 0);
		assert(llvm_value != nullptr);
		assert(id != nullptr);
		assert(type != nullptr);
		assert_implies(llvm::dyn_cast<llvm::GlobalVariable>(llvm_value) != nullptr, is_global);
	}

	virtual ~bound_var_t() throw() {}

	location_t internal_location;
	atom const name;
	bound_type_t::ref const type;
	identifier::ref const id;

private:
	llvm::Value * const llvm_value;
	bool const _is_global;

public:
	llvm::Value *get_llvm_value() const;
	std::string str() const;

	bool is_ref() const;
	bool is_global() const;
	bool is_int() const;
	bool is_pointer() const;
	types::signature get_signature() const;

	typedef ptr<const bound_var_t> ref;
	typedef std::vector<ref> refs;
	typedef std::map<types::signature, ref> overloads;
	typedef std::weak_ptr<bound_var_t> weak_ref;
	typedef std::map<atom, overloads> map;

	types::type_t::ref get_type() const;
	virtual location_t get_location() const;

public:
	llvm::Value *resolve_bound_var_value(llvm::IRBuilder<> &builder) const;
	ref resolve_bound_value(status_t &status, llvm::IRBuilder<> &builder, ptr<scope_t> scope) const;

	static ref create(
			location_t internal_location,
			atom name,
			bound_type_t::ref type,
			llvm::Value *llvm_value,
			identifier::ref id,
			bool is_global);

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
			atom name,
			identifier::ref id,
			ptr<module_scope_t> module_scope);

	static ref create(
			location_t internal_location,
			atom name,
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
std::vector<llvm::Value *> get_llvm_values(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ptr<scope_t> scope,
		location_t location,
		ptr<const types::type_args_t> type_args,
	   	const bound_var_t::refs &vars);
bound_var_t::ref resolve_alloca(llvm::IRBuilder<> &builder, bound_var_t::ref var);
