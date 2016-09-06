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
	scope_t() = delete;
	scope_t(const scope_t &scope) = delete;

	scope_t(atom name, types::term::map type_env) : name(name), type_env(type_env) {}

	/* general methods */
	std::string str();
	ptr<function_scope_t> new_function_scope(atom name);

	/* scope interface */
	virtual ptr<program_scope_t> get_program_scope();
	virtual ptr<scope_t> get_parent_scope() = 0;
	virtual ptr<const scope_t> get_parent_scope() const = 0;

	virtual void dump(std::ostream &os) const = 0;

	bool has_bound_variable(atom symbol, resolution_constraints_t resolution_constraints);

	bound_var_t::ref get_bound_variable(status_t &status, const ptr<const ast::item> &obj, atom symbol);
	bound_var_t::ref maybe_get_bound_variable(atom symbol);

	void put_bound_variable(atom symbol, bound_var_t::ref bound_variable);

	virtual bound_type_t::ref get_bound_type(types::signature signature);

	std::string get_name() const;

	std::string make_fqn(std::string leaf_name) const;

	virtual llvm::Module *get_llvm_module();

	/* find all checked and unchecked functions that have the name given by the
	 * symbol parameter */
	virtual void get_callables(atom symbol, var_t::refs &fns);
	ptr<module_scope_t> get_module_scope();

	bound_var_t::ref get_singleton(atom name);

	void put_type_term(atom name, types::term::ref type_term);
	types::term::map get_type_env() const;

protected:
	atom name;

	bound_var_t::map bound_vars;
	types::term::map type_env;
};

typedef bound_type_t::ref return_type_constraint_t;

struct runnable_scope_t : public scope_t {
	/* runnable scopes are those that can instantiate local scopes */
	typedef ptr<runnable_scope_t> ref;

	virtual ~runnable_scope_t() throw() {}

	runnable_scope_t(atom name, types::term::map type_env) : scope_t(name, type_env) {}
	runnable_scope_t() = delete;
	runnable_scope_t(const runnable_scope_t &) = delete;

	virtual ptr<local_scope_t> new_local_scope(atom name) = 0;
	virtual return_type_constraint_t &get_return_type_constraint() = 0;

	void check_or_update_return_type_constraint(
			status_t &status,
		   	const ptr<const ast::item> &return_statement,
		   	return_type_constraint_t return_type);
};

struct module_scope_t : public scope_t {
	typedef ptr<module_scope_t> ref;
	typedef std::map<atom, ref> map;

	module_scope_t() = delete;
	module_scope_t(atom name, ptr<program_scope_t> parent_scope, llvm::Module *llvm_module);
	virtual ~module_scope_t() throw() {}

	ptr<program_scope_t> parent_scope;

	llvm::Module * const llvm_module;

	virtual void get_callables(atom symbol, var_t::refs &fns);
	// virtual ptr<module_scope_t> get_module(atom symbol) const;

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
	void mark_checked(const ptr<const ast::item> &node);
	virtual llvm::Module *get_llvm_module();

	virtual void dump(std::ostream &os) const;

	std::set<ptr<const ast::item>> visited;

	static module_scope_t::ref create(atom module_name, ptr<program_scope_t> parent_scope, llvm::Module *llvm_module);

	// void add_linked_module(status_t &status, ptr<const ast::item> obj, atom symbol, module_scope_t::ref module_scope);

private:
	/* modules can have all sorts of bound and unchecked vars and types */
	unchecked_var_t::map unchecked_vars;
	unchecked_type_t::map unchecked_types;

public:
	/* let code look at the ordered list for iteration purposes */
	unchecked_var_t::refs unchecked_vars_ordered;
	unchecked_type_t::refs unchecked_types_ordered;
};

std::string str(const module_scope_t::map &modules);

/* scope keeps tabs on the bindings of variables, noting their declared
 * type as it goes */
struct program_scope_t : public scope_t {
	typedef ptr<program_scope_t> ref;

	program_scope_t(atom name, types::term::map type_env) : scope_t(name, type_env) {}
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

	static program_scope_t::ref create(atom name);

	/* this is meant to be called when we know we're looking in program scope.
	 * this is not an implementation of get_symbol.  */
	module_scope_t::ref lookup_module(atom symbol);
	std::string dump_llvm_modules();

	virtual bound_type_t::ref get_bound_type(types::signature signature);
	bool put_bound_type(bound_type_t::ref type);

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

struct generic_substitution_scope_t : public scope_t {
	typedef ptr<generic_substitution_scope_t> ref;

	generic_substitution_scope_t(
			atom name,
		   	scope_t::ref parent_scope,
		   	types::type::ref callee_signature) :
	   	scope_t(name, parent_scope->get_type_env()),
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
