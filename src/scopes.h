#pragma once
#include "zion.h"
#include "env.h"
#include <string>
#include "token.h"
#include "ast_decls.h"
#include <set>
#include "unchecked_type.h"
#include "unchecked_var.h"
#include "signature.h"

extern const token_kind SCOPE_TK;
extern const char *SCOPE_SEP;
extern const char SCOPE_SEP_CHAR;
extern const char *GLOBAL_ID;

struct scope_t;
struct life_t;

enum resolution_constraints_t {
	rc_capture_level,
};

struct compiler_t;
struct program_scope_t;
struct module_scope_t;
struct function_scope_t;
struct runnable_scope_t;
struct generic_substitution_scope_t;
struct closure_scope_t;

typedef std::map<std::string, std::pair<bool /*is_structural*/, ptr<const types::type_t> > > env_map_t;

struct scope_t : public env_t {
	typedef ptr<scope_t> ref;
	typedef ptr<const scope_t> cref;

	virtual ~scope_t() {}
	virtual ref this_scope() = 0;
	virtual cref this_scope() const = 0;
	// virtual ptr<closure_scope_t> get_closure_scope() = 0;
	virtual bool has_bound(const std::string &name, const types::type_t::ref &type, bound_var_t::ref *var=nullptr) const = 0;
	virtual bool symbol_exists_in_running_scope(std::string symbol, bound_var_t::ref &bound_var) = 0;
	virtual bound_type_t::ref get_bound_type(types::signature signature, bool use_mappings=true) = 0;
	virtual bound_var_t::ref get_bound_function(std::string name, std::string signature) = 0;
	virtual bound_var_t::ref get_bound_variable(llvm::IRBuilder<> &builder, location_t location, std::string symbol, scope_t::ref stopping_scope=nullptr) = 0;
	virtual bound_var_t::ref get_singleton(std::string name) = 0;
	virtual llvm::Module *get_llvm_module() = 0;
	virtual ptr<const module_scope_t> get_module_scope() const = 0;
	virtual ptr<const program_scope_t> get_program_scope() const = 0;
	virtual ptr<const scope_t> get_parent_scope() const = 0;
	virtual ptr<function_scope_t> new_function_scope(std::string name) = 0;
	virtual ptr<module_scope_t> get_module_scope() = 0;
	virtual ptr<program_scope_t> get_program_scope() = 0;
	virtual ptr<scope_t> get_parent_scope() = 0;
	virtual std::string get_leaf_name() const = 0;
	virtual std::string get_name() const = 0;
	virtual std::string make_fqn(std::string leaf_name) const = 0;
	virtual std::string str() = 0;
	virtual types::type_t::map get_type_variable_bindings() const = 0;
	virtual void dump(std::ostream &os) const = 0;
	virtual void get_callables(std::string symbol, var_t::refs &fns, bool check_unchecked=true) = 0;
	virtual void put_bound_variable(std::string symbol, bound_var_t::ref bound_variable) = 0;
    virtual void put_nominal_typename(const std::string &name, types::type_t::ref expansion) = 0;
    virtual void put_structural_typename(const std::string &name, types::type_t::ref expansion) = 0;
    virtual void put_type_variable_binding(const std::string &binding, types::type_t::ref type) = 0;
};

typedef bound_type_t::ref return_type_constraint_t;

struct closure_scope_t;

struct runnable_scope_t : public virtual scope_t {
	/* runnable scopes are those that can instantiate local scopes */
	typedef ptr<runnable_scope_t> ref;

	virtual ~runnable_scope_t() {}

	virtual ptr<runnable_scope_t> new_runnable_scope(std::string name) = 0;
	virtual ptr<closure_scope_t> new_closure_scope(llvm::IRBuilder<> &builder, std::string name) = 0;
	virtual return_type_constraint_t &get_return_type_constraint() = 0;
	virtual void check_or_update_return_type_constraint(const ptr<const ast::item_t> &return_statement, return_type_constraint_t return_type) = 0;
	virtual void set_innermost_loop_bbs(llvm::BasicBlock *new_loop_continue_bb, llvm::BasicBlock *new_loop_break_bb) = 0;

	virtual llvm::BasicBlock *get_innermost_loop_break() const = 0;
	virtual llvm::BasicBlock *get_innermost_loop_continue() const = 0;
};

struct closure_scope_t : public virtual scope_t {
	typedef ptr<closure_scope_t> ref;
	virtual ~closure_scope_t() {}

	virtual void set_capture_env(bound_var_t::ref capture_env) = 0;
	virtual bound_var_t::ref create_closure(llvm::IRBuilder<> &builder, ptr<life_t> life, location_t location, bound_var_t::ref function) = 0;
};

struct loop_tracker_t {
	/* use dtors + the call stack to manage the basic block jumps for loops */
	loop_tracker_t(runnable_scope_t::ref scope, llvm::BasicBlock *loop_continue_bb, llvm::BasicBlock *loop_break_bb);
	~loop_tracker_t();

private:
	runnable_scope_t::ref scope;
	llvm::BasicBlock *prior_loop_continue_bb;
	llvm::BasicBlock *prior_loop_break_bb;
};

struct module_scope_t : public virtual scope_t {
	typedef ptr<module_scope_t> ref;
	typedef std::map<std::string, ref> map;

	virtual ~module_scope_t() {}

	virtual unchecked_var_t::ref put_unchecked_variable(std::string symbol, unchecked_var_t::ref unchecked_variable) = 0;
	virtual void put_unchecked_type(unchecked_type_t::ref unchecked_type) = 0;
	virtual unchecked_type_t::ref get_unchecked_type(std::string symbol) = 0;

	/* module checking management
	 * after checking a function regardless of whether it was generic or not
	 * we'll mark it as checked so we don't try to check it again. if it was generic
	 * then it won't make sense to check it again at the top level since it's not being
	 * instantiated. if it is not generic, then there's no need to check it because
	 * it's already instantiated. */
	virtual unchecked_type_t::refs &get_unchecked_types_ordered() = 0;
	virtual void dump_tags(std::ostream &os) const = 0;
};

std::string str(const module_scope_t::map &modules);


struct program_scope_t : public virtual module_scope_t {
	typedef ptr<program_scope_t> ref;

	virtual ~program_scope_t() {}

	virtual ptr<module_scope_t> new_module_scope(std::string name, llvm::Module *llvm_module) = 0;

	static program_scope_t::ref create(std::string name, compiler_t &compiler, llvm::Module *llvm_module);

	virtual bound_var_t::ref upsert_init_module_vars_function(llvm::IRBuilder<> &builder) = 0;
	virtual void set_insert_point_to_init_module_vars_function(llvm::IRBuilder<> &builder, std::string for_var_decl_name) = 0;

	virtual llvm::Type *get_llvm_type(location_t location, std::string type_name) = 0;
	virtual llvm::Function *get_llvm_function(location_t location, std::string function_name) = 0;

	/* this is meant to be called when we know we're looking in program scope.
	 * this is not an implementation of get_symbol.  */
	virtual module_scope_t::ref lookup_module(std::string symbol) = 0;
	virtual std::string dump_llvm_modules() = 0;

	virtual unchecked_var_t::ref get_unchecked_variable(std::string symbol) = 0;

	virtual void put_bound_type(bound_type_t::ref type) = 0;
	virtual void put_bound_type_mapping(types::signature source, types::signature dest) = 0;

	virtual unchecked_var_t::refs &get_unchecked_vars_ordered() = 0;
	virtual bound_type_t::ref get_runtime_type(llvm::IRBuilder<> &builder, std::string name, bool get_ptr=false) = 0;
};

struct function_scope_t : public virtual runnable_scope_t {
	typedef ptr<function_scope_t> ref;

	virtual ~function_scope_t() {}

	static function_scope_t::ref create(std::string module_name, scope_t::ref parent_scope);
	virtual void set_return_type_constraint(return_type_constraint_t return_type_constraint) = 0;
};

struct generic_substitution_scope_t : public virtual scope_t {
	virtual ~generic_substitution_scope_t() {}

	static ref create(
		   	llvm::IRBuilder<> &builder,
		   	const ptr<const ast::item_t> &fn_decl,
		   	scope_t::ref module_scope,
			unification_t unification,
			types::type_t::ref callee_type);
};

void put_bound_function(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		std::string function_name,
		identifier::ref extends_module,
		bound_var_t::ref bound_function,
		runnable_scope_t::ref *new_scope);

