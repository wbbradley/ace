#pragma once
#include "zion.h"
#include "dbg.h"
#include "status.h"
#include "utils.h"
#include <string>
#include <map>
#include "ast_decls.h"
#include "types.h"
#include "signature.h"

struct bound_var_t;

struct bound_type_t {
	typedef ptr<const bound_type_t> ref;
	typedef std::weak_ptr<const bound_type_t> weak_ref;
	typedef std::vector<std::pair<atom, ref>> named_pairs;
	typedef std::vector<ref> refs;
	typedef std::map<types::signature, ref> map;
	typedef atom::map<int> name_index;

	bound_type_t(
			types::type_t::ref type,
			location_t location,
			llvm::Type *llvm_type,
			llvm::Type *llvm_specific_type);

	virtual ~bound_type_t() {}

protected:
	bound_type_t(const bound_type_t &) = delete;
	bound_type_t(const bound_type_t &&) = delete;
	bound_type_t &operator =(const bound_type_t &) = delete;

public:
	bool is_function() const;
	bool is_void() const;
	void is_managed_ptr(status_t &status, llvm::IRBuilder<> &builder, ptr<scope_t> scope, bool &is_managed) const;
	bool is_ptr(ptr<scope_t> scope) const;
	bool is_ref() const;
	bool is_maybe() const;
    bool is_module() const;
	bool is_opaque() const;
	types::signature get_signature() const;

	std::string str() const;
	types::type_t::ref get_type() const;
	bool is_concrete() const;
	location_t const get_location() const;
	llvm::Type *get_llvm_type() const;
	llvm::Type *get_llvm_specific_type() const;

	static refs refs_from_vars(const std::vector<ptr<const bound_var_t>> &vars);

	ref get_pointer() const;
	static ref create(
			types::type_t::ref type,
			location_t location,
			llvm::Type *llvm_type,
			llvm::Type *llvm_specific_type = nullptr);

private:
	types::type_t::ref type;
	const location_t location;
	llvm::Type * const llvm_type;
	llvm::Type * const llvm_specific_type;
};

types::type_t::refs get_types(const bound_type_t::refs &bound_types);
types::type_t::ref get_tuple_type(const bound_type_t::refs &items_types);
types::type_args_t::ref get_args_type(bound_type_t::refs args);
types::type_function_t::ref get_function_type(types::type_t::ref context, bound_type_t::named_pairs named_args, bound_type_t::ref ret);

std::string str(const bound_type_t::refs &args);
std::string str(const bound_type_t::named_pairs &named_pairs);
std::string str(const bound_type_t::name_index &name_index);
std::ostream &operator <<(std::ostream &os, const bound_type_t &type);

types::type_function_t::ref get_function_type(types::type_t::ref context, bound_type_t::refs args, bound_type_t::ref return_type);
