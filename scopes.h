#pragma once
#include "zion.h"
#include <string>
#include "token.h"
#include "ast_decls.h"
#include <unordered_map>
#include <set>
#include "unchecked_type.h"
#include "unchecked_var.h"
#include "signature.h"

#define SCOPE_SEP "::"

struct scope_t;

enum resolution_constraints_t {
	rc_all_scopes,
	rc_just_current_scope,
	rc_capture_level,
};

struct program_scope_t;
struct module_scope_t;
struct function_scope_t;
struct local_scope_t;
struct generic_substitution_scope_t;

struct scope_t : public std::enable_shared_from_this<scope_t> {
	typedef ptr<scope_t> ref;
	typedef ptr<const scope_t> cref;

	virtual ~scope_t() throw() {}
	virtual atom get_leaf_name() const = 0;

	/* general methods */
	virtual std::string str() = 0;
	virtual ptr<function_scope_t> new_function_scope(atom name) = 0;
	virtual ptr<program_scope_t> get_program_scope() = 0;
	virtual ptr<scope_t> get_parent_scope() = 0;
	virtual ptr<const scope_t> get_parent_scope() const = 0;
	virtual void dump(std::ostream &os) const = 0;
	virtual bool has_bound_variable(atom symbol, resolution_constraints_t resolution_constraints) = 0;

	virtual bound_var_t::ref get_bound_variable(status_t &status, const ptr<const ast::item> &obj, atom symbol) = 0;
	virtual bound_var_t::ref maybe_get_bound_variable(atom symbol) = 0;
	virtual void put_bound_variable(status_t &status, atom symbol, bound_var_t::ref bound_variable) = 0;
	virtual bound_type_t::ref get_bound_type(types::signature signature) = 0;
	virtual std::string get_name() const;
	virtual std::string make_fqn(std::string leaf_name) const = 0;
	virtual llvm::Module *get_llvm_module();

	/* find all checked and unchecked functions that have the name given by the
	 * symbol parameter */
	virtual void get_callables(atom symbol, var_t::refs &fns) = 0;
	ptr<module_scope_t> get_module_scope();

	virtual bound_var_t::ref get_singleton(atom name) = 0;

	virtual void put_type_term(status_t &status, atom name, types::term::ref type_term) = 0;
	virtual void put_type_decl_term(atom name, types::term::ref type_term) = 0;
	virtual types::term::map get_type_env() const = 0;
	virtual types::term::map get_type_decl_env() const = 0;
};

template <typename BASE>
struct scope_impl_t : public BASE {
	typedef ptr<scope_t> ref;
	typedef ptr<const scope_t> cref;

	virtual ~scope_impl_t() throw() {}
	scope_impl_t() = delete;
	scope_impl_t(const scope_impl_t &scope) = delete;

	virtual atom get_leaf_name() const {
		return name;
	}

	scope_impl_t(atom name, types::term::map type_env) : name(name), type_env(type_env) {}

	ptr<function_scope_t> new_function_scope(atom name);
	ptr<program_scope_t> get_program_scope();
	void put_type_term(status_t &status, atom name, types::term::ref type_term);
	void put_type_decl_term(atom name, types::term::ref type_term);
	types::term::map get_type_env() const;
	types::term::map get_type_decl_env() const;
	std::string str();
	void put_bound_variable(status_t &status, atom symbol, bound_var_t::ref bound_variable);
	bool has_bound_variable(atom symbol, resolution_constraints_t resolution_constraints);
	bound_var_t::ref get_singleton(atom name);
	bound_var_t::ref maybe_get_bound_variable(atom symbol);
	bound_var_t::ref get_bound_variable(status_t &status, const ptr<const ast::item> &obj, atom symbol);
	std::string make_fqn(std::string leaf_name) const;
	bound_type_t::ref get_bound_type(types::signature signature);
	void get_callables(atom symbol, var_t::refs &fns);

protected:
	atom name;

	bound_var_t::map bound_vars;
	types::term::map type_env;
	types::term::map type_decl_env;
};

typedef bound_type_t::ref return_type_constraint_t;

struct runnable_scope_t : public scope_impl_t<scope_t> {
	/* runnable scopes are those that can instantiate local scopes */
	typedef ptr<runnable_scope_t> ref;

	virtual ~runnable_scope_t() throw() {}

	runnable_scope_t(atom name, types::term::map type_env) : scope_impl_t(name, type_env) {}
	runnable_scope_t() = delete;
	runnable_scope_t(const runnable_scope_t &) = delete;

	virtual ptr<local_scope_t> new_local_scope(atom name) = 0;
	virtual return_type_constraint_t &get_return_type_constraint() = 0;

	void check_or_update_return_type_constraint(
			status_t &status,
		   	const ptr<const ast::item> &return_statement,
		   	return_type_constraint_t return_type);
};

struct module_scope_t : scope_t {
	typedef ptr<module_scope_t> ref;
	typedef std::map<atom, ref> map;

	virtual ~module_scope_t() throw() {}

	virtual void put_unchecked_type(status_t &status, unchecked_type_t::ref unchecked_type) = 0;
	virtual unchecked_type_t::ref get_unchecked_type(atom symbol) = 0;

	virtual unchecked_var_t::ref put_unchecked_variable(atom symbol, unchecked_var_t::ref unchecked_variable) = 0;

	/* module checking management
	 * after checking a function regardless of whether it was generic or not
	 * we'll mark it as checked so we don't try to check it again. if it was generic
	 * then it won't make sense to check it again at the top level since it's not being
	 * instantiated. if it is not generic, then there's no need to check it because
	 * it's already instantiated. */
	virtual bool has_checked(const ptr<const ast::item> &node) const = 0;
	virtual void mark_checked(status_t &status, const ptr<const ast::item> &node) = 0;
	virtual llvm::Module *get_llvm_module() = 0;
	virtual unchecked_type_t::refs &get_unchecked_types_ordered() = 0;
	virtual unchecked_var_t::refs &get_unchecked_vars_ordered() = 0;
};

struct module_scope_impl_t : public scope_impl_t<module_scope_t> {
	typedef ptr<module_scope_impl_t> ref;
	typedef std::map<atom, ref> map;

	module_scope_impl_t() = delete;
	module_scope_impl_t(atom name, ptr<program_scope_t> parent_scope, llvm::Module *llvm_module);
	virtual ~module_scope_impl_t() throw() {}

	ptr<program_scope_t> parent_scope;

	llvm::Module * const llvm_module;

	virtual void get_callables(atom symbol, var_t::refs &fns);

	void put_unchecked_type(status_t &status, unchecked_type_t::ref unchecked_type);
	unchecked_type_t::ref get_unchecked_type(atom symbol);

	unchecked_var_t::ref put_unchecked_variable(atom symbol, unchecked_var_t::ref unchecked_variable);

	virtual ptr<scope_t> get_parent_scope();
	virtual ptr<const scope_t> get_parent_scope() const;

	/* module checking management
	 * after checking a function regardless of whether it was generic or not
	 * we'll mark it as checked so we don't try to check it again. if it was generic
	 * then it won't make sense to check it again at the top level since it's not being
	 * instantiated. if it is not generic, then there's no need to check it because
	 * it's already instantiated. */
	bool has_checked(const ptr<const ast::item> &node) const;
	void mark_checked(status_t &status, const ptr<const ast::item> &node);
	virtual llvm::Module *get_llvm_module();

	virtual void dump(std::ostream &os) const;

	std::set<ptr<const ast::item>> visited;

	static module_scope_t::ref create(atom module_name, ptr<program_scope_t> parent_scope, llvm::Module *llvm_module);

	// void add_linked_module(status_t &status, ptr<const ast::item> obj, atom symbol, module_scope_impl_t::ref module_scope);

	virtual unchecked_type_t::refs &get_unchecked_types_ordered();
	virtual unchecked_var_t::refs &get_unchecked_vars_ordered();

protected:
	/* modules can have all sorts of bound and unchecked vars and types */
	unchecked_var_t::map unchecked_vars;
	unchecked_type_t::map unchecked_types;

	/* let code look at the ordered list for iteration purposes */
	unchecked_var_t::refs unchecked_vars_ordered;
	unchecked_type_t::refs unchecked_types_ordered;
};

std::string str(const module_scope_t::map &modules);

/* scope keeps tabs on the bindings of variables, noting their declared
 * type as it goes */
struct program_scope_t : public module_scope_impl_t {
	typedef ptr<program_scope_t> ref;

	program_scope_t(atom name, llvm::Module *llvm_module) :
	   	module_scope_impl_t(name, nullptr, llvm_module) {}

	program_scope_t() = delete;
	virtual ~program_scope_t() throw() {}

	virtual ptr<program_scope_t> get_program_scope();
	virtual void dump(std::ostream &os) const;

	virtual ptr<scope_t> get_parent_scope() {
        return nullptr;
    }

	virtual ptr<const scope_t> get_parent_scope() const {
		return nullptr;
	}

	ptr<module_scope_t> new_module_scope(atom name, llvm::Module *llvm_module);

	static program_scope_t::ref create(atom name, llvm::Module *llvm_module);

	/* this is meant to be called when we know we're looking in program scope.
	 * this is not an implementation of get_symbol.  */
	module_scope_t::ref lookup_module(atom symbol);
	std::string dump_llvm_modules();

	unchecked_var_t::ref put_unchecked_variable(atom symbol, unchecked_var_t::ref unchecked_variable);

	virtual bound_type_t::ref get_bound_type(types::signature signature);
	void put_bound_type(bound_type_t::ref type);

private:
	module_scope_t::map modules;
	bound_type_t::map bound_types;
};

struct function_scope_t : public runnable_scope_t {
	typedef ptr<function_scope_t> ref;

	virtual ~function_scope_t() throw() {}
	function_scope_t(atom name, scope_t::ref parent_scope) :
	   	runnable_scope_t(name, parent_scope->get_type_env()), parent_scope(parent_scope) {}

	virtual void dump(std::ostream &os) const;

	virtual ptr<scope_t> get_parent_scope();
	virtual ptr<const scope_t> get_parent_scope() const;

	virtual return_type_constraint_t &get_return_type_constraint();
	virtual ptr<local_scope_t> new_local_scope(atom name);

	scope_t::ref parent_scope;

	/* functions have return type constraints for use during type checking */
	return_type_constraint_t return_type_constraint;

	static function_scope_t::ref create(atom module_name, scope_t::ref parent_scope);
};

struct local_scope_t : public runnable_scope_t {
	typedef ptr<local_scope_t> ref;

	local_scope_t(atom name, scope_t::ref parent_scope, return_type_constraint_t &return_type_constraint);

	virtual ~local_scope_t() throw() {}
	virtual void dump(std::ostream &os) const;
	virtual return_type_constraint_t &get_return_type_constraint();
	virtual ptr<local_scope_t> new_local_scope(atom name);
	virtual ptr<scope_t> get_parent_scope();
	virtual ptr<const scope_t> get_parent_scope() const;

	scope_t::ref parent_scope;
	return_type_constraint_t &return_type_constraint;

	static local_scope_t::ref create(
			atom name,
		   	scope_t::ref parent_scope,
			types::term::map type_env,
		   	return_type_constraint_t &return_type_constraint);
};

struct generic_substitution_scope_t : public scope_impl_t<scope_t> {
	typedef ptr<generic_substitution_scope_t> ref;

	generic_substitution_scope_t(
			atom name,
		   	scope_t::ref parent_scope,
		   	types::type::ref callee_signature) :
	   	scope_impl_t(name, parent_scope->get_type_env()),
	   	callee_signature(callee_signature),
	   	parent_scope(parent_scope) {}

	virtual ~generic_substitution_scope_t() throw() {}
	virtual ptr<scope_t> get_parent_scope();
	virtual ptr<const scope_t> get_parent_scope() const;
	virtual llvm::Module *get_llvm_module();

	virtual void dump(std::ostream &os) const;

	static ref create(
			status_t &status,
		   	llvm::IRBuilder<> &builder,
		   	const ptr<const ast::item> &fn_decl,
		   	scope_t::ref module_scope,
			unification_t unification,
			types::type::ref callee_type);

	const types::type::ref callee_signature;

private:
	scope_t::ref parent_scope;
};

template <typename T>
ptr<function_scope_t> scope_impl_t<T>::new_function_scope(atom name) {
	return function_scope_t::create(name, this->shared_from_this());
}

template <typename T>
ptr<program_scope_t> scope_impl_t<T>::get_program_scope() {
	return this->get_parent_scope()->get_program_scope();
}

template <typename T>
void scope_impl_t<T>::put_type_term(status_t &status, atom name, types::term::ref type_term) {
	debug_above(2, log(log_info, "registering type term " c_term("%s") " as %s",
				name.c_str(), type_term->str().c_str()));
	if (type_env.find(name) == type_env.end()) {
		type_env[name] = type_term;
	} else {
		user_error(status, type_term->get_id()->get_location(),
				"multiple supertypes are not yet implemented (" c_type("%s") " <: " c_type("%s") ")",
				name.c_str(), type_term->str().c_str());
	}
}

template <typename T>
void scope_impl_t<T>::put_type_decl_term(atom name, types::term::ref type_term) {
	auto iter = type_decl_env.find(name);
	if (iter == type_decl_env.end()) {
		debug_above(2, log(log_info, "registering type decl term " c_term("%s") " as %s",
					name.c_str(), type_term->str().c_str()));
		type_decl_env[name] = type_term;
	} else {
		debug_above(8, log(log_info, "type decl term " c_term("%s") " has already been registered as %s",
					name.c_str(), type_decl_env[name]->str().c_str()));
		/* this term may have already been registered. */
		assert(type_decl_env[name]->str() == type_term->str());
	}
}

template <typename T>
types::term::map scope_impl_t<T>::get_type_env() const {
	auto parent_scope = this->get_parent_scope();
	if (parent_scope != nullptr) {
		return merge(parent_scope->get_type_env(), type_env);
	} else {
		return type_env;
	}
}

template <typename T>
types::term::map scope_impl_t<T>::get_type_decl_env() const {
	auto parent_scope = this->get_parent_scope();
	if (parent_scope != nullptr) {
		return merge(parent_scope->get_type_decl_env(), type_decl_env);
	} else {
		return type_decl_env;
	}
}

template <typename T>
std::string scope_impl_t<T>::str() {
	std::stringstream ss;
	scope_t::ref p = this->shared_from_this();
	do {
		p->dump(ss);
	} while ((p = p->get_parent_scope()) != nullptr);
	return ss.str();
}

template <typename T>
void scope_impl_t<T>::put_bound_variable(
		status_t &status,
	   	atom symbol,
	   	bound_var_t::ref bound_variable)
{
	debug_above(8, log(log_info, "binding %s", bound_variable->str().c_str()));

	auto &resolve_map = bound_vars[symbol];
	types::signature signature = bound_variable->get_signature();
	auto existing_bound_var_iter = resolve_map.find(signature);
	if (existing_bound_var_iter != resolve_map.end()) {
		auto existing_bound_var = existing_bound_var_iter->second;

		user_error(status, bound_variable->get_location(),
			   "failed to bind %s as its name and signature are already taken",
			   bound_variable->str().c_str());

		user_error(status, bound_variable->get_location(),
			   "see existing bound variable %s",
			   existing_bound_var->str().c_str());

	} else {
		resolve_map[signature] = bound_variable;
	}
}

template <typename T>
bool scope_impl_t<T>::has_bound_variable(
		atom symbol,
		resolution_constraints_t resolution_constraints)
{
	auto iter = bound_vars.find(symbol);
	if (iter != bound_vars.end()) {
		/* we found this symbol */
		return true;
	} else if (auto parent_scope = this->get_parent_scope()) {
		/* we did not find the symbol, let's consider looking higher up the
		 * scopes */
		switch (resolution_constraints) {
		case rc_all_scopes:
			return parent_scope->has_bound_variable(symbol,
					resolution_constraints);
		case rc_just_current_scope:
			return false;
		case rc_capture_level:
			if (dynamic_cast<const function_scope_t *>(this)) {
				return false;
			} else {
				return parent_scope->has_bound_variable(symbol,
						resolution_constraints);
			}
		}
	} else {
		/* we're at the top and we still didn't find it, quit. */
		return false;
	}
}

template <typename T>
bound_var_t::ref scope_impl_t<T>::get_singleton(atom name) {
	/* there can be only one */
	auto &coll = bound_vars;
	assert(coll.begin() != coll.end());
	auto iter = coll.find(name);
	assert(iter != coll.end());
	auto &resolve_map = iter->second;
	assert(resolve_map.begin() != resolve_map.end());
	auto resolve_iter = resolve_map.begin();
	auto item = resolve_iter->second;
	assert(++resolve_iter == resolve_map.end());
	return item;
}

template <typename T>
bound_var_t::ref scope_impl_t<T>::maybe_get_bound_variable(atom symbol) {
	auto iter = bound_vars.find(symbol);
	if (iter != bound_vars.end()) {
		const auto &overloads = iter->second;
		if (overloads.size() == 0) {
			panic("we have an empty list of overloads");
			return nullptr;
		} else if (overloads.size() == 1) {
			return overloads.begin()->second;
		} else {
			return nullptr;
		}
	} else if (auto parent_scope = this->get_parent_scope()) {
		return parent_scope->maybe_get_bound_variable(symbol);
	}

	return nullptr;
}

bound_var_t::ref get_bound_variable_from_scope(
		status_t &status,
		const ptr<const ast::item> &obj,
		atom symbol,
		bound_var_t::map bound_vars,
		scope_t::ref parent_scope);

template <typename T>
bound_var_t::ref scope_impl_t<T>::get_bound_variable(
		status_t &status,
	   	const ptr<const ast::item> &obj,
	   	atom symbol)
{
	return ::get_bound_variable_from_scope(status, obj, symbol, bound_vars, this->get_parent_scope());
}

template <typename T>
std::string scope_impl_t<T>::make_fqn(std::string leaf_name) const {
	return this->get_name() + std::string(SCOPE_SEP) + leaf_name;
}

bound_type_t::ref get_bound_type_from_scope(
		types::signature signature,
		std::string fqn_signature,
		program_scope_t::ref program_scope,
	   	scope_t::ref parent_scope);

template <typename T>
bound_type_t::ref scope_impl_t<T>::get_bound_type(types::signature signature) {
	return get_bound_type_from_scope(signature,
			this->make_fqn(signature.repr().str()), get_program_scope(),
			this->get_parent_scope());
}

void get_callables_from_bound_vars(
		atom symbol,
		const bound_var_t::map &bound_vars,
		var_t::refs &fns);

template <typename T>
void scope_impl_t<T>::get_callables(atom symbol, var_t::refs &fns) {
	/* default scope behavior is to look at bound variables */
	get_callables_from_bound_vars(symbol, bound_vars, fns);

	if (auto parent_scope = this->get_parent_scope()) {
		/* let's see if our parent scope has any of this symbol */
		parent_scope->get_callables(symbol, fns);
	}
}


