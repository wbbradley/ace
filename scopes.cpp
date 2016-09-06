#include "zion.h"
#include "dbg.h"
#include "scopes.h"
#include "ast.h"
#include "utils.h"
#include "llvm_utils.h"
#include "llvm_types.h"
#include "unification.h"

#define SCOPE_SEP "::"

std::string scope_t::get_name() const {
	auto parent_scope = this->get_parent_scope();
	if (parent_scope) {
		return parent_scope->get_name() + SCOPE_SEP + name.str();
	} else {
		return name.str();
	}
}

ptr<program_scope_t> program_scope_t::get_program_scope() {
	return std::static_pointer_cast<program_scope_t>(shared_from_this());
}

program_scope_t::ref scope_t::get_program_scope() {
	return get_parent_scope()->get_program_scope();
}

ptr<module_scope_t> scope_t::get_module_scope() {
	if (auto module_scope = dyncast<module_scope_t>(shared_from_this())) {
		return module_scope;
	} else {
		auto parent_scope = get_parent_scope();
		if (parent_scope != nullptr) {
			return parent_scope->get_module_scope();
		} else {
			return nullptr;
		}
	}
}

void scope_t::put_type_term(atom name, types::term::ref type_term) {
	debug_above(2, log(log_info, "registering type term " c_term("%s") " as %s",
			name.c_str(), type_term->str().c_str()));
	type_env[name] = type_term;
}

types::term::map scope_t::get_type_env() const {
	return type_env;
}

std::string scope_t::str() {
	std::stringstream ss;
	scope_t::ref p = shared_from_this();
	do {
		p->dump(ss);
	} while ((p = p->get_parent_scope()) != nullptr);
	return ss.str();
}

void scope_t::put_bound_variable(atom symbol, bound_var_t::ref bound_variable) {
	debug_above(8, log(log_info, "binding %s", bound_variable->str().c_str()));

	auto &resolve_map = bound_vars[symbol];
	types::signature signature = bound_variable->get_signature();
	if (resolve_map.find(signature) != resolve_map.end()) {
		panic(string_format("we can't be adding variables with the same signature to the same scope (" c_var("%s") ": %s)",
					symbol.c_str(), signature.str().c_str()));
	}
	resolve_map[signature] = bound_variable;
}

bool scope_t::has_bound_variable(
		atom symbol,
	   	resolution_constraints_t resolution_constraints)
{
	auto iter = bound_vars.find(symbol);
	if (iter != bound_vars.end()) {
		/* we found this symbol */
		return true;
	} else if (auto parent_scope = get_parent_scope()) {
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

bound_var_t::ref scope_t::get_singleton(atom name) {
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

bound_var_t::ref scope_t::maybe_get_bound_variable(atom symbol) {
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
	} else if (auto parent_scope = get_parent_scope()) {
		return parent_scope->maybe_get_bound_variable(symbol);
	}

	return nullptr;
}

bound_var_t::ref scope_t::get_bound_variable(status_t &status, const ast::item::ref &obj, atom symbol) {
	auto iter = bound_vars.find(symbol);
	if (iter != bound_vars.end()) {
		const bound_var_t::overloads &overloads = iter->second;
		if (overloads.size() == 0) {
			panic("we have an empty list of overloads");
			return nullptr;
		} else if (overloads.size() == 1) {
			return overloads.begin()->second;
		} else {
			assert(overloads.size() > 1);
			user_error(status, *obj, "a non-callsite reference to an overloaded variable usage %s was found. overloads at this immediate location are:\n%s",
					obj->token.str().c_str(),
					::str(overloads).c_str());
			return nullptr;
		}
	} else if (auto parent_scope = get_parent_scope()) {
		return parent_scope->get_bound_variable(status, obj, symbol);
	}

	debug_above(3, log(log_info, "no bound variable found for %s", 
				obj->token.str().c_str()));
	return nullptr;
}

llvm::Module *scope_t::get_llvm_module() {
	if (get_parent_scope()) {
		return get_parent_scope()->get_llvm_module();
	} else {
		assert(false);
		return nullptr;
	}
}

std::string scope_t::make_fqn(std::string leaf_name) const {
	return get_name() + std::string(SCOPE_SEP) + leaf_name;
}

bound_type_t::ref scope_t::get_bound_type(types::signature signature) {
	auto full_signature = types::signature{make_fqn(signature.repr().str())};
	auto bound_type = get_program_scope()->get_bound_type(full_signature);
	if (bound_type != nullptr) {
		return bound_type;
	} else {
		return get_parent_scope()->get_bound_type(signature);
	}
}

bound_type_t::ref program_scope_t::get_bound_type(types::signature signature) {
	auto iter = bound_types.find(signature);
	if (iter != bound_types.end()) {
		return iter->second;
	} else {
		return nullptr;
	}
}

function_scope_t::ref function_scope_t::create(atom module_name, scope_t::ref parent_scope) {
	return make_ptr<function_scope_t>(module_name, parent_scope);
}

ptr<scope_t> function_scope_t::get_parent_scope() {
	return parent_scope;
}

ptr<const scope_t> function_scope_t::get_parent_scope() const {
	return parent_scope;
}

local_scope_t::ref local_scope_t::create(
		atom name,
		scope_t::ref parent_scope,
		types::term::map type_env,
		return_type_constraint_t &return_type_constraint)
{
	return make_ptr<local_scope_t>(name, parent_scope, return_type_constraint);
}

ptr<function_scope_t> scope_t::new_function_scope(atom name) {
	return function_scope_t::create(name, shared_from_this());
}

void get_callables_from_bound_vars(
		atom symbol,
		const bound_var_t::map &bound_vars,
		var_t::refs &fns)
{
	auto iter = bound_vars.find(symbol);
	if (iter != bound_vars.end()) {
		const auto &overloads = iter->second;
		for (auto &pair : overloads) {
			auto &var = pair.second;
			if (var->type->is_function()) {
				fns.push_back(var);
			}
		}
	}
}

void get_callables_from_unchecked_vars(
		atom symbol,
		const unchecked_var_t::map &unchecked_vars,
		var_t::refs &fns)
{
	auto iter = unchecked_vars.find(symbol);
	if (iter != unchecked_vars.end()) {
		const unchecked_var_t::overload_vector &overloads = iter->second;
		for (auto &var : overloads) {
			assert(dyncast<const ast::data_ctor>(var->node) ||
					dyncast<const ast::function_defn>(var->node) ||
					dyncast<const ast::type_product>(var->node));
			fns.push_back(var);
		}
	}
}

void scope_t::get_callables(atom symbol, var_t::refs &fns) {
	/* default scope behavior is to look at bound variables */
	get_callables_from_bound_vars(symbol, bound_vars, fns);

	if (auto parent_scope = get_parent_scope()) {
		/* let's see if our parent scope has any of this symbol */
		parent_scope->get_callables(symbol, fns);
	}
}

void module_scope_t::get_callables(atom symbol, var_t::refs &fns) {
	/* default scope behavior is to look at bound variables */
	get_callables_from_bound_vars(symbol, bound_vars, fns);
	get_callables_from_unchecked_vars(symbol, unchecked_vars, fns);

	if (auto parent_scope = get_parent_scope()) {
		/* let's see if our parent scope has any of this symbol */
		parent_scope->get_callables(symbol, fns);
	}
}

ptr<local_scope_t> function_scope_t::new_local_scope(atom name) {
	return local_scope_t::create(name, shared_from_this(), type_env, return_type_constraint);
}

ptr<local_scope_t> local_scope_t::new_local_scope(atom name) {
	return local_scope_t::create(name, shared_from_this(), type_env, return_type_constraint);
}

return_type_constraint_t &function_scope_t::get_return_type_constraint() {
	return return_type_constraint;
}

return_type_constraint_t &local_scope_t::get_return_type_constraint() {
	return return_type_constraint;
}

ptr<scope_t> local_scope_t::get_parent_scope() {
	return parent_scope;
}

ptr<const scope_t> local_scope_t::get_parent_scope() const {
	return parent_scope;
}

void runnable_scope_t::check_or_update_return_type_constraint(
		status_t &status,
		const ast::item::ref &return_statement,
		bound_type_t::ref return_type)
{
	return_type_constraint_t &return_type_constraint = get_return_type_constraint();
	if (return_type_constraint == nullptr) {
		return_type_constraint = return_type;
		debug_above(5, log(log_info, "set return type to %s", return_type_constraint->str().c_str()));
	} else {
		unification_t unification = unify(
				return_type_constraint->type->to_term(),
				return_type->type->to_term(),
				get_type_env());

		if (!unification.result) {
			// TODO: consider directional unification here
			// TODO: consider storing more useful info in return_type_constraint
			user_error(status, *return_statement, "return expression type %s does not match %s",
					return_type->str().c_str(), return_type_constraint->str().c_str());
		} else {
			/* this return type checks out */
			debug_above(2, log(log_info, "unified %s :> %s",
						return_type_constraint->str().c_str(),
						return_type->str().c_str()));
		}
	}
}

local_scope_t::local_scope_t(
		atom name,
		scope_t::ref parent_scope,
		return_type_constraint_t &return_type_constraint) :
   	runnable_scope_t(name, parent_scope->get_type_env()),
   	parent_scope(parent_scope),
   	return_type_constraint(return_type_constraint)
{
}

void dump_bindings(
		std::ostream &os,
		const bound_var_t::map &bound_vars,
		const bound_type_t::map &bound_types)
{
	os << "bound:\n";
	for (auto &var_pair : bound_vars) {
		os << C_VAR << var_pair.first << C_RESET << ": ";
		const auto &overloads = var_pair.second;
		os << ::str(overloads);
	}

	for (auto &type_pair : bound_types) {
		os << C_TYPE << type_pair.first << C_RESET << ": ";
		os << *type_pair.second << std::endl;
	}
}

void dump_bindings(
		std::ostream &os,
		const unchecked_var_t::map &unchecked_vars,
		const unchecked_type_t::map &unchecked_types)
{
	os << "unchecked:\n";
	for (auto &var_pair : unchecked_vars) {
		os << C_UNCHECKED << var_pair.first << C_RESET << ": [";
		const unchecked_var_t::overload_vector &overloads = var_pair.second;
		const char *sep = "";
		for (auto &var_overload : overloads) {
			os << sep << var_overload->node->token.str();
			sep = ", ";
		}
		os << "]" << std::endl;
	}

	for (auto &type_pair : unchecked_types) {
		os << C_TYPE << type_pair.first << C_RESET << ": ";
		os << type_pair.second->node->token.str() << std::endl;
	}
}

void dump_linked_modules(std::ostream &os, const module_scope_t::map &modules) {
	os << "modules: " << str(modules) << std::endl;
}

void program_scope_t::dump(std::ostream &os) const {
	os << std::endl << "PROGRAM SCOPE: " << name << std::endl;
	dump_bindings(os, bound_vars, bound_types);
}

void module_scope_t::dump(std::ostream &os) const {
	os << std::endl << "MODULE SCOPE: " << name << std::endl;
	dump_bindings(os, bound_vars, {});
	dump_bindings(os, unchecked_vars, unchecked_types);
	// dump_linked_modules(os, linked_modules);
}

void function_scope_t::dump(std::ostream &os) const {
	os << std::endl << "FUNCTION SCOPE: " << name << std::endl;
	dump_bindings(os, bound_vars, {});
}

void local_scope_t::dump(std::ostream &os) const {
	os << std::endl << "LOCAL SCOPE: " << name << std::endl;
	dump_bindings(os, bound_vars, {});
}

void generic_substitution_scope_t::dump(std::ostream &os) const {
	os << std::endl << "GENERIC SUBSTITUTION SCOPE: " << name << std::endl;
	dump_bindings(os, bound_vars, {});
}

module_scope_t::module_scope_t(atom name, program_scope_t::ref parent_scope, llvm::Module *llvm_module) :
	scope_t(name, parent_scope->get_type_env()), parent_scope(parent_scope), llvm_module(llvm_module)
{
}

bool module_scope_t::has_checked(const ptr<const ast::item> &node) const {
	return visited.find(node) != visited.end();
}

void module_scope_t::mark_checked(const ptr<const ast::item> &node) {
	if (auto function_defn = dyncast<const ast::function_defn>(node)) {
		if (is_function_defn_generic(shared_from_this(), *function_defn)) {
			/* for now let's never mark generic functions as checked, until we
			 * have a mechanism to join the term to the checked-mark.  */
			return;
		}
	}

	assert(!has_checked(node));
	visited.insert(node);
}

void module_scope_t::put_unchecked_type(
		status_t &status,
		unchecked_type_t::ref unchecked_type)
{
	auto iter_bool = unchecked_types.insert({unchecked_type->name, unchecked_type});
	if (!iter_bool.second) {
		/* this unchecked type already exists */
		user_error(status, *unchecked_type->node, "type term already exists (see %s)",
				iter_bool.first->second->str().c_str());
	}

	/* also keep an ordered list of the unchecked types */
	unchecked_types_ordered.push_back(unchecked_type);
}

unchecked_type_t::ref module_scope_t::get_unchecked_type(atom symbol) {
	auto iter = unchecked_types.find(symbol);
	if (iter != unchecked_types.end()) {
		return iter->second;
	} else {
		return nullptr;
	}
}

unchecked_var_t::ref module_scope_t::put_unchecked_variable(
		atom symbol,
		unchecked_var_t::ref unchecked_variable)
{
	debug_above(2, log(log_info, "registering an unchecked variable %s as %s",
				symbol.c_str(),
				unchecked_variable->str().c_str()));

	auto iter = unchecked_vars.find(symbol);
	if (iter != unchecked_vars.end()) {
		/* this variable already exists, let's consider overloading it */
		if (dyncast<const ast::function_defn>(unchecked_variable->node)) {
			iter->second.push_back(unchecked_variable);
		} else {
			assert(!"why are we putting this here?");
		}
	} else {
		unchecked_vars[symbol] = {unchecked_variable};
	}

	/* also keep a list of the order in which we encountered these */
	unchecked_vars_ordered.push_back(unchecked_variable);
	return unchecked_variable;
}

bool program_scope_t::put_bound_type(bound_type_t::ref type) {
	debug_above(8, log(log_info, "binding type %s as " c_id("%s"),
				type->str().c_str(),
				type->get_signature().repr().c_str()));
	bound_types[type->get_signature().repr()] = type;
	return false;
}

ptr<module_scope_t> program_scope_t::new_module_scope(
		atom name,
		llvm::Module *llvm_module)
{
	assert(!lookup_module(name));
	auto module_scope = module_scope_t::create(name, get_program_scope(), llvm_module);
	modules.insert({name, module_scope});
	return module_scope;
}

std::string str(const module_scope_t::map &modules) {
	std::stringstream ss;
	const char *sep = "";
	ss << "[";
	for (auto &pair : modules) {
		ss << sep << pair.first;
		sep = ", ";
	}
	ss << "]";
	return ss.str();
}

module_scope_t::ref program_scope_t::lookup_module(atom symbol) {
	auto iter = modules.find(symbol);
	if (iter != modules.end()) {
		return iter->second;
	} else {
		debug_above(4, log(log_warning, "no module named " c_module("%s") " in %s",
					symbol.c_str(),
					::str(modules).c_str()));
		return nullptr;
	}
}

std::string program_scope_t::dump_llvm_modules() {
	std::stringstream ss;
	for (auto &module_pair : modules) {
		ss << C_MODULE << "MODULE " << C_RESET << module_pair.first << std::endl;
		ss << llvm_print_module(*module_pair.second->llvm_module);
	}
	return ss.str();
}

ptr<scope_t> module_scope_t::get_parent_scope() {
	return parent_scope;
}

ptr<const scope_t> module_scope_t::get_parent_scope() const {
	return parent_scope;
}

module_scope_t::ref module_scope_t::create(
		atom name,
		program_scope_t::ref parent_scope,
		llvm::Module *llvm_module)
{
	return make_ptr<module_scope_t>(name, parent_scope, llvm_module);
}

llvm::Module *module_scope_t::get_llvm_module() {
	return llvm_module;
}

llvm::Module *generic_substitution_scope_t::get_llvm_module() {
	return get_parent_scope()->get_llvm_module();
}

program_scope_t::ref program_scope_t::create(atom name) {
	return make_ptr<program_scope_t>(name, types::term::map{});
}

ptr<scope_t> generic_substitution_scope_t::get_parent_scope() {
	return parent_scope;
}

ptr<const scope_t> generic_substitution_scope_t::get_parent_scope() const {
	return parent_scope;
}

generic_substitution_scope_t::ref generic_substitution_scope_t::create(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ptr<const ast::item> &fn_decl,
		scope_t::ref parent_scope,
		unification_t unification,
		types::type::ref callee_type)
{
	/* instantiate a new scope */
	auto subst_scope = make_ptr<generic_substitution_scope_t>(
			"generic substitution", parent_scope, callee_type);

	/* iterate over the bindings found during unifications and make
	 * substitutions in the type environment */
	for (auto &pair : unification.bindings) {
		if (pair.first.str().find("_") != 0) {
			auto bound_type = upsert_bound_type(
					status,
					builder,
					parent_scope,
					pair.second);
					
			if (!bound_type) {
				user_error(status, *fn_decl, "when trying to instantiate %s, couldn't find or create type %s",
						fn_decl->token.str().c_str(),
						pair.second->str().c_str());
				return nullptr;
			} else {
				/* the substitution scope allows us to masquerade a generic name as
				 * a bound type */
				auto term = pair.second->to_term(unification.bindings);
				debug_above(5, log(log_info, "adding " c_id("%s") " to env as %s",
							pair.first.c_str(),
							term->str().c_str()));
				subst_scope->type_env[pair.first] = term;
			}
		} else {
			debug_above(7, log(log_info, "skipping adding %s to generic substitution scope",
						pair.first.c_str()));
		}
	}
	return subst_scope;
}
