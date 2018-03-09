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

enum resolution_constraints_t {
	rc_capture_level,
};

struct compiler_t;
struct program_scope_t;
struct module_scope_t;
struct function_scope_t;
struct local_scope_t;
struct generic_substitution_scope_t;

struct scope_t : public std::enable_shared_from_this<scope_t>, public env_t {
	typedef ptr<scope_t> ref;
	typedef ptr<const scope_t> cref;

	virtual ~scope_t() {}
	virtual std::string get_leaf_name() const = 0;

	/* general methods */
	virtual std::string str() = 0;
	virtual ptr<function_scope_t> new_function_scope(std::string name) = 0;
	virtual ptr<program_scope_t> get_program_scope() = 0;
	virtual ptr<const program_scope_t> get_program_scope() const = 0;
	virtual ptr<scope_t> get_parent_scope() = 0;
	virtual ptr<const scope_t> get_parent_scope() const = 0;
	virtual void dump(std::ostream &os) const = 0;
	virtual bool symbol_exists_in_running_scope(std::string symbol, bound_var_t::ref &bound_var) = 0;

	virtual bound_var_t::ref get_bound_function(std::string name, std::string signature) = 0;
	virtual bound_var_t::ref get_bound_variable(location_t location, std::string symbol, bool search_parents=true) = 0;
	virtual void put_bound_variable(std::string symbol, bound_var_t::ref bound_variable) = 0;
	virtual bound_type_t::ref get_bound_type(types::signature signature, bool use_mappings=true) = 0;
	virtual std::string get_name() const;
	virtual std::string make_fqn(std::string leaf_name) const = 0;
	virtual llvm::Module *get_llvm_module();
	/* find all checked and unchecked functions that have the name given by the
	 * symbol parameter */
	virtual void get_callables(std::string symbol, var_t::refs &fns, bool check_unchecked=true) = 0;
	ptr<module_scope_t> get_module_scope();
	ptr<const module_scope_t> get_module_scope() const;

	virtual bound_var_t::ref get_singleton(std::string name) = 0;
	virtual bool has_bound(const std::string &name, const types::type_t::ref &type, bound_var_t::ref *var=nullptr) const = 0;

    /* Then, there are mappings from type_variable names to type_t::refs */
	virtual types::type_t::map get_type_variable_bindings() const = 0;

    virtual void put_nominal_typename(const std::string &name, types::type_t::ref expansion) = 0;
    virtual void put_structural_typename(const std::string &name, types::type_t::ref expansion) = 0;
    virtual void put_type_variable_binding(const std::string &binding, types::type_t::ref type) = 0;
};

template <typename BASE>
struct scope_impl_t : public BASE {
	typedef ptr<scope_t> ref;
	typedef ptr<const scope_t> cref;

	virtual ~scope_impl_t() throw() {}
	scope_impl_t(ptr<scope_t> parent_scope) = delete;
	scope_impl_t(const scope_impl_t &scope) = delete;

	virtual std::string get_leaf_name() const {
		return scope_name;
	}

	scope_impl_t(std::string name, ptr<scope_t> parent_scope) :
        scope_name(name),
		parent_scope(parent_scope) {}

	ptr<function_scope_t> new_function_scope(std::string name);
	ptr<program_scope_t> get_program_scope();
	ptr<const program_scope_t> get_program_scope() const;
	types::type_t::map get_type_variable_bindings() const;
	std::string str();
	void put_bound_variable(std::string symbol, bound_var_t::ref bound_variable);
	virtual bool symbol_exists_in_running_scope(std::string symbol, bound_var_t::ref &bound_var);
	bound_var_t::ref get_singleton(std::string name);
	bound_var_t::ref get_bound_function(std::string name, std::string signature);
	bound_var_t::ref get_bound_variable(location_t location, std::string symbol, bool search_parents=true);
	std::string make_fqn(std::string leaf_name) const;
	bound_type_t::ref get_bound_type(types::signature signature, bool use_mappings=true);
	void get_callables(std::string symbol, var_t::refs &fns, bool check_unchecked=true);
    virtual void put_nominal_typename(const std::string &name, types::type_t::ref expansion);
    virtual void put_structural_typename(const std::string &name, types::type_t::ref expansion);
    virtual void put_type_variable_binding(const std::string &binding, types::type_t::ref type);
	virtual ptr<scope_t> get_parent_scope();
	virtual ptr<const scope_t> get_parent_scope() const;
	virtual bool has_bound(const std::string &name, const types::type_t::ref &type, bound_var_t::ref *var=nullptr) const;
	virtual types::type_t::ref get_nominal_type(const std::string &name) const;
	virtual types::type_t::ref get_total_type(const std::string &name) const;

protected:
	std::string scope_name;

	ref parent_scope;
	bound_var_t::map bound_vars;
	types::type_t::map nominal_env;
	types::type_t::map structural_env;
	types::type_t::map type_variable_bindings;
};

typedef bound_type_t::ref return_type_constraint_t;

struct runnable_scope_t : public scope_impl_t<scope_t> {
	/* runnable scopes are those that can instantiate local scopes */
	typedef ptr<runnable_scope_t> ref;

	virtual ~runnable_scope_t() throw() {}

	runnable_scope_t(std::string name, scope_t::ref parent_scope);
	runnable_scope_t() = delete;
	runnable_scope_t(const runnable_scope_t &) = delete;

	virtual ptr<local_scope_t> new_local_scope(std::string name) = 0;
	virtual return_type_constraint_t &get_return_type_constraint() = 0;

	void check_or_update_return_type_constraint(
		   	const ptr<const ast::item_t> &return_statement,
		   	return_type_constraint_t return_type);

	llvm::BasicBlock *get_innermost_loop_break() const;
	llvm::BasicBlock *get_innermost_loop_continue() const;

private:
	friend struct loop_tracker_t;
	void set_innermost_loop_bbs(llvm::BasicBlock *loop_continue_bb, llvm::BasicBlock *loop_break_bb);
	llvm::BasicBlock *loop_break_bb = nullptr;
	llvm::BasicBlock *loop_continue_bb = nullptr;
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

struct module_scope_t : scope_t {
	typedef ptr<module_scope_t> ref;
	typedef std::map<std::string, ref> map;

	virtual ~module_scope_t() throw() {}

	unchecked_var_t::ref put_unchecked_variable(std::string symbol, unchecked_var_t::ref unchecked_variable);
	virtual void put_unchecked_type(unchecked_type_t::ref unchecked_type) = 0;
	virtual unchecked_type_t::ref get_unchecked_type(std::string symbol) = 0;

	/* module checking management
	 * after checking a function regardless of whether it was generic or not
	 * we'll mark it as checked so we don't try to check it again. if it was generic
	 * then it won't make sense to check it again at the top level since it's not being
	 * instantiated. if it is not generic, then there's no need to check it because
	 * it's already instantiated. */
	virtual llvm::Module *get_llvm_module() = 0;
	virtual unchecked_type_t::refs &get_unchecked_types_ordered() = 0;
	virtual void dump_tags(std::ostream &os) const = 0;
};

struct module_scope_impl_t : public scope_impl_t<module_scope_t> {
	typedef ptr<module_scope_impl_t> ref;
	typedef std::map<std::string, ref> map;

	module_scope_impl_t() = delete;
	module_scope_impl_t(std::string name, ptr<program_scope_t> parent_scope, llvm::Module *llvm_module);
	virtual ~module_scope_impl_t() throw() {}

	llvm::Module * const llvm_module;
	virtual std::string make_fqn(std::string leaf_name) const;

	void put_unchecked_type(unchecked_type_t::ref unchecked_type);
	unchecked_type_t::ref get_unchecked_type(std::string symbol);

	virtual unchecked_type_t::refs &get_unchecked_types_ordered();

	/* module checking management
	 * after checking a function regardless of whether it was generic or not
	 * we'll mark it as checked so we don't try to check it again. if it was generic
	 * then it won't make sense to check it again at the top level since it's not being
	 * instantiated. if it is not generic, then there's no need to check it because
	 * it's already instantiated. */
	virtual llvm::Module *get_llvm_module();

	virtual void dump(std::ostream &os) const;
	virtual void dump_tags(std::ostream &os) const;

	static module_scope_t::ref create(std::string module_name, ptr<program_scope_t> parent_scope, llvm::Module *llvm_module);
	virtual bool symbol_exists_in_running_scope(std::string symbol, bound_var_t::ref &bound_var);
	virtual bool has_bound(const std::string &name, const types::type_t::ref &type, bound_var_t::ref *var=nullptr) const;

protected:
	/* modules can have unchecked types */
	unchecked_type_t::map unchecked_types;

	/* let code look at the ordered list for iteration purposes */
	unchecked_type_t::refs unchecked_types_ordered;
};

std::string str(const module_scope_t::map &modules);



/* scope keeps tabs on the bindings of variables, noting their declared
 * type as it goes */
struct program_scope_t : public module_scope_impl_t {
	typedef ptr<program_scope_t> ref;

	program_scope_t(
			std::string name,
			compiler_t &compiler,
		   	llvm::Module *llvm_module) :
	   	module_scope_impl_t(name, nullptr, llvm_module),
		compiler(compiler) {}

	program_scope_t() = delete;
	virtual ~program_scope_t() throw() {}

	virtual std::string make_fqn(std::string name) const;
	virtual ptr<program_scope_t> get_program_scope();
	virtual ptr<const program_scope_t> get_program_scope() const;
	virtual void dump(std::ostream &os) const;

	ptr<module_scope_t> new_module_scope(std::string name, llvm::Module *llvm_module);

	static program_scope_t::ref create(std::string name, compiler_t &compiler, llvm::Module *llvm_module);

	bound_var_t::ref upsert_init_module_vars_function(llvm::IRBuilder<> &builder);
	void set_insert_point_to_init_module_vars_function(llvm::IRBuilder<> &builder, std::string for_var_decl_name);

	virtual void get_callables(std::string symbol, var_t::refs &fns, bool check_unchecked=true);
	llvm::Type *get_llvm_type(location_t location, std::string type_name);
	llvm::Function *get_llvm_function(location_t location, std::string function_name);

	/* this is meant to be called when we know we're looking in program scope.
	 * this is not an implementation of get_symbol.  */
	module_scope_t::ref lookup_module(std::string symbol);
	std::string dump_llvm_modules();

	unchecked_var_t::ref get_unchecked_variable(std::string symbol);
	unchecked_var_t::ref put_unchecked_variable(std::string symbol, unchecked_var_t::ref unchecked_variable);

	virtual bound_type_t::ref get_bound_type(types::signature signature, bool use_mappings=true);
	void put_bound_type(bound_type_t::ref type);
	void put_bound_type_mapping(types::signature source, types::signature dest);

	unchecked_var_t::map unchecked_vars;

	virtual unchecked_var_t::refs &get_unchecked_vars_ordered();
	bound_type_t::ref get_runtime_type(llvm::IRBuilder<> &builder, std::string name, bool get_ptr=false);
	virtual void dump_tags(std::ostream &os) const;

private:
	compiler_t &compiler;
	module_scope_t::map modules;
	bound_type_t::map bound_types;
	std::map<types::signature, types::signature> bound_type_mappings;

	/* track the module var initialization function */
	bound_var_t::ref init_module_vars_function;
	unchecked_var_t::refs initialized_module_vars;

	/* let code look at the ordered list for iteration purposes */
	unchecked_var_t::refs unchecked_vars_ordered;
};

struct function_scope_t : public runnable_scope_t {
	typedef ptr<function_scope_t> ref;

	virtual ~function_scope_t() throw() {}
	function_scope_t(std::string name, scope_t::ref parent_scope) :
	   	runnable_scope_t(name, parent_scope) {}

	virtual void dump(std::ostream &os) const;

	virtual return_type_constraint_t &get_return_type_constraint();
	virtual ptr<local_scope_t> new_local_scope(std::string name);

	scope_t::ref parent_scope;

	/* functions have return type constraints for use during type checking */
	return_type_constraint_t return_type_constraint;

	static function_scope_t::ref create(std::string module_name, scope_t::ref parent_scope);
};

struct local_scope_t : public runnable_scope_t {
	typedef ptr<local_scope_t> ref;

	local_scope_t(std::string name, scope_t::ref parent_scope, return_type_constraint_t &return_type_constraint);

	virtual ~local_scope_t() throw() {}
	virtual void dump(std::ostream &os) const;
	virtual return_type_constraint_t &get_return_type_constraint();
	virtual ptr<local_scope_t> new_local_scope(std::string name);

	return_type_constraint_t &return_type_constraint;

	static local_scope_t::ref create(
			std::string name,
		   	scope_t::ref parent_scope,
		   	return_type_constraint_t &return_type_constraint);
};

struct generic_substitution_scope_t : public scope_impl_t<scope_t> {
	typedef ptr<generic_substitution_scope_t> ref;

	generic_substitution_scope_t(
			std::string name,
		   	scope_t::ref parent_scope,
		   	types::type_t::ref callee_signature);

	virtual ~generic_substitution_scope_t() throw() {}
	virtual llvm::Module *get_llvm_module();

	virtual void dump(std::ostream &os) const;

	static ref create(
		   	llvm::IRBuilder<> &builder,
		   	const ptr<const ast::item_t> &fn_decl,
		   	scope_t::ref module_scope,
			unification_t unification,
			types::type_t::ref callee_type);

	const types::type_t::ref callee_signature;
};

template <typename T>
ptr<function_scope_t> scope_impl_t<T>::new_function_scope(std::string name) {
	return function_scope_t::create(name, this->scope_t::shared_from_this());
}

template <typename T>
ptr<program_scope_t> scope_impl_t<T>::get_program_scope() {
	assert(!dynamic_cast<program_scope_t* const>(this));
	return this->get_parent_scope()->get_program_scope();
}

template <typename T>
ptr<const program_scope_t> scope_impl_t<T>::get_program_scope() const {
	assert(!dynamic_cast<const program_scope_t* const>(this));
	return this->get_parent_scope()->get_program_scope();
}

void put_typename_impl(
		scope_t::ref parent_scope,
		const std::string &scope_name,
		types::type_t::map &typename_env,
		const std::string &type_name,
		types::type_t::ref expansion,
		bool is_structural);

template <typename T>
void scope_impl_t<T>::put_structural_typename(const std::string &type_name, types::type_t::ref expansion) {
	assert(nominal_env.find(type_name) == nominal_env.end());
	put_typename_impl(get_parent_scope(), scope_name, structural_env, type_name, expansion, true /*is_structural*/);
}

template <typename T>
void scope_impl_t<T>::put_nominal_typename(const std::string &type_name, types::type_t::ref expansion) {
	assert(structural_env.find(type_name) == structural_env.end());
	put_typename_impl(get_parent_scope(), scope_name, nominal_env, type_name, expansion, false /*is_structural*/);
}

template <typename T>
void scope_impl_t<T>::put_type_variable_binding(const std::string &name, types::type_t::ref type) {
	auto iter = type_variable_bindings.find(name);
	if (iter == type_variable_bindings.end()) {
		debug_above(2, log(log_info, "binding type variable " c_type("%s") " as %s",
					name.c_str(), type->str().c_str()));
		type_variable_bindings[name] = type;
	} else {
		debug_above(8, log(log_info, "type variable " c_type("%s") " has already been bound as %s",
					name.c_str(), type_variable_bindings[name]->str().c_str()));
		/* this term may have already been registered. */
		assert(type_variable_bindings[name]->str() == type->str());
	}
}

#if 0
template <typename T>
types::type_t::map scope_impl_t<T>::get_nominal_env() const {
	auto parent_scope = this->get_parent_scope();
	if (parent_scope != nullptr) {
		return merge(parent_scope->get_nominal_env(), nominal_env);
	} else {
		return nominal_env;
	}
}

template <typename T>
types::type_t::map scope_impl_t<T>::get_total_env() const {
	auto parent_scope = this->get_parent_scope();
	if (parent_scope != nullptr) {
		return merge(
				parent_scope->get_total_env(),
				nominal_env,
				structural_env);
	} else {
		return merge(nominal_env, structural_env);
	}
}
#endif

template <typename T>
types::type_t::ref scope_impl_t<T>::get_nominal_type(const std::string &name) const {
	assert(false);
	return nullptr;
}

template <typename T>
types::type_t::ref scope_impl_t<T>::get_total_type(const std::string &name) const {
	assert(false);
	return nullptr;
}

template <typename T>
types::type_t::map scope_impl_t<T>::get_type_variable_bindings() const {
	auto parent_scope = this->get_parent_scope();
	if (parent_scope != nullptr) {
		return merge(parent_scope->get_type_variable_bindings(), type_variable_bindings);
	} else {
		return type_variable_bindings;
	}
}

template <typename T>
std::string scope_impl_t<T>::str() {
	std::stringstream ss;
	scope_t::ref p = this->scope_t::shared_from_this();
	do {
		p->dump(ss);
	} while ((p = p->get_parent_scope()) != nullptr);
	return ss.str();
}

template <typename T>
std::string scope_impl_t<T>::make_fqn(std::string leaf_name) const {
	return get_parent_scope()->make_fqn(leaf_name);
}

template <typename T>
void scope_impl_t<T>::put_bound_variable(
	   	std::string symbol,
	   	bound_var_t::ref bound_variable)
{
	debug_above(4, log(log_info, "binding variable " c_id("%s") " in " c_id("%s") " to %s at %s",
				symbol.c_str(),
				this->get_name().c_str(),
			   	bound_variable->str().c_str(),
				bound_variable->get_location().str().c_str()));

	auto &resolve_map = bound_vars[symbol];
	types::signature signature = bound_variable->get_signature();
	auto existing_bound_var_iter = resolve_map.find(signature);
	if (existing_bound_var_iter != resolve_map.end()) {
		auto error = user_error(bound_variable->get_location(), "symbol " c_id("%s") " is already bound (signature is %s)", symbol.c_str(),
				signature.str().c_str());
		error.add_info(existing_bound_var_iter->second->get_location(), "see existing bound variable");
		throw error;
	} else {
		resolve_map[signature] = bound_variable;
		if (!dynamic_cast<program_scope_t *>(this)
				&& dynamic_cast<module_scope_t *>(this))
		{
			auto program_scope = get_program_scope();
			program_scope->put_bound_variable(this->make_fqn(symbol), bound_variable);
		}
	}
}

template <typename T>
bool scope_impl_t<T>::symbol_exists_in_running_scope(
		std::string symbol,
		bound_var_t::ref &bound_var)
{
	auto iter = bound_vars.find(symbol);
	if (iter != bound_vars.end()) {
		assert(iter->second.size() == 1);
		/* we found this symbol */
		bound_var = iter->second.begin()->second;
		return true;
	} else if (auto parent_scope = this->get_parent_scope()) {
		/* we did not find the symbol, let's consider looking higher up the
		 * scopes */
		if (dynamic_cast<const function_scope_t *>(this)) {
			return false;
		} else {
			return parent_scope->symbol_exists_in_running_scope(symbol, bound_var);
		}
	} else {
		/* we're at the top and we still didn't find it, quit. */
		return false;
	}
}

template <typename T>
bound_var_t::ref scope_impl_t<T>::get_singleton(std::string name) {
	/* there can be only one */
	auto &coll = bound_vars;
	assert(coll.begin() != coll.end());
	auto iter = coll.find(name);
	if (iter != coll.end()) {
		auto &resolve_map = iter->second;
		assert(resolve_map.begin() != resolve_map.end());
		auto resolve_iter = resolve_map.begin();
		auto item = resolve_iter->second;
		assert(++resolve_iter == resolve_map.end());
		return item;
	} else {
		panic(string_format("could not find singleton " c_id("%s"),
					name.c_str()));
		return nullptr;
	}
}

bound_var_t::ref get_bound_variable_from_scope(
		location_t location,
		std::string scope_name,
		std::string symbol,
		const bound_var_t::map &bound_vars,
		scope_t::ref parent_scope);
 
template <typename T>
bound_var_t::ref scope_impl_t<T>::get_bound_function(
        std::string name,
        std::string signature)
{
   auto iter = bound_vars.find(name);
   if (iter != bound_vars.end()) {
       auto &resolve_map = iter->second;
       auto overload_iter = resolve_map.find(signature);
       if (overload_iter != resolve_map.end()) {
           return overload_iter->second;
       } else {
           debug_above(7, log("couldn't find %s : %s in %s",
                   name.c_str(),
                   signature.c_str(),
                   ::str(resolve_map).c_str()));
       }
   }

   return nullptr;
}

template <typename T>
bound_var_t::ref scope_impl_t<T>::get_bound_variable(
		location_t location,
		std::string symbol,
		bool search_parents)
{
	return ::get_bound_variable_from_scope(location, this->get_name(),
			symbol, bound_vars, search_parents ? this->get_parent_scope() : nullptr);
}

bound_type_t::ref get_bound_type_from_scope(
		types::signature signature,
		program_scope_t::ref program_scope, bool use_mappings);

template <typename T>
bound_type_t::ref scope_impl_t<T>::get_bound_type(types::signature signature, bool use_mappings) {
	return get_bound_type_from_scope(signature, this->get_program_scope(), use_mappings);
}

template <typename T>
bool scope_impl_t<T>::has_bound(const std::string &name, const types::type_t::ref &type, bound_var_t::ref *var) const {
	return get_parent_scope()->has_bound(name, type, var);
}



void get_callables_from_bound_vars(
		ptr<scope_t> scope,
		std::string symbol,
		const bound_var_t::map &bound_vars,
		var_t::refs &fns);

template <typename T>
void scope_impl_t<T>::get_callables(
		std::string symbol,
	   	var_t::refs &fns,
	   	bool check_unchecked)
{
	// TODO: clean up this horrible mess
	auto module_scope = dynamic_cast<module_scope_t*>(this);

	if (module_scope != nullptr) {
		/* default scope behavior is to look at bound variables */
		get_callables_from_bound_vars(scope_t::shared_from_this(), symbol, bound_vars, fns);

		if (parent_scope != nullptr) {
			/* let's see if our parent scope has any of this symbol from our scope */
			parent_scope->get_callables(
				symbol.find(SCOPE_SEP) == std::string::npos
					? make_fqn(symbol)
					: symbol,
			   	fns,
				check_unchecked);

			/* let's see if our parent scope has any of this symbol just generally */
			parent_scope->get_callables(symbol, fns, check_unchecked);
		}
	} else if (parent_scope != nullptr) {
		return parent_scope->get_callables(symbol, fns, check_unchecked);
	} else {
		assert(false);
		return;
	}
}

template <typename T>
ptr<scope_t> scope_impl_t<T>::get_parent_scope() {
	return parent_scope;
}

template <typename T>
ptr<const scope_t> scope_impl_t<T>::get_parent_scope() const {
	return parent_scope;
}

void put_bound_function(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		std::string function_name,
		identifier::ref extends_module,
		bound_var_t::ref bound_function,
		local_scope_t::ref *new_scope);
