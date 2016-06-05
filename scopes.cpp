#include "zion.h"
#include "dbg.h"
#include "scopes.h"
#include "ast.h"
#include "utils.h"
#include "llvm_utils.h"
#include "llvm_types.h"
#include "unification.h"

#define SCOPE_SEP "::"

std::string scope_t::get_name() {
	auto parent_scope = this->get_parent_scope();
	if (parent_scope) {
		return parent_scope->get_name() + SCOPE_SEP + name.str();
	} else {
		return name.str();
	}
}

ptr<module_scope_t> module_scope_t::get_module_scope() {
	return null_impl(); // const_cast<module_scope_t*>(this)->shared_from_this();
}

ptr<program_scope_t> program_scope_t::get_program_scope() {
	return std::static_pointer_cast<program_scope_t>(shared_from_this());
}

ptr<module_scope_t> scope_t::get_module_scope() {
	assert(false);
	return nullptr;
}

program_scope_t::ref scope_t::get_program_scope() {
	return get_parent_scope()->get_program_scope();
}

types::term::map scope_t::get_type_env() const {
	assert(false);
	// TODO: build a copy of the recursively built type env
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
	log(log_info, "binding %s", bound_variable->str().c_str());

	auto &resolve_map = bound_vars[symbol];
	types::signature signature = bound_variable->get_signature();
	if (resolve_map.find(signature) != resolve_map.end()) {
		panic(string_format("we can't be adding variables with the same signature to the same scope (" c_var("%s") ": %s)",
					symbol.c_str(), signature.str().c_str()));
	}
	resolve_map[signature] = bound_variable;
}

bool put_bound_type(types::signature signature, bound_type_t::ref bound_type) {
	log(log_info, "storing type %s in scope as %s",
			bound_type->str().c_str(),
			signature.str().c_str());

	/* check some preconditions first */
	if (bound_type->is_function()) {
		llvm::Type *llvm_type = bound_type->llvm_type;
		if (llvm_type->isPointerTy()) {
			llvm_type = llvm_type->getPointerElementType();
		}
		assert(llvm_type->isFunctionTy());
	}
	assert(false);
	return false;
}

#if 0
	assert(type_env.find(name) == type_env.end() ||
		   	type_env[name].str() == type_term.str());
	type_env.insert({name, type_term});
}
#endif

#if 0
bound_type_t::ref scope_t::upsert_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		types::type::term term,
		bound_type_t::refs args,
		bound_type_t::ref return_type,
		llvm::Type *llvm_type,
		ast::item::ref obj)
{
	/* look at the term, and if it already exists in this scope, return
	 * it. otherwise, create it with the given params, put it into the current
	 * scope and return it. */
	if (args.size() != 0 || return_type != nullptr) {
		/* if we've got args or a return type then this should be a function */
		assert(term.is_function());
	}

	assert(!term.is_generic());

	bound_type_t::ref bound_type = maybe_get_bound_type(term);

	if (bound_type != nullptr) {
		/* we've already seen this thing */
		return bound_type;
	} else if (llvm_type == nullptr) {
		assert(obj != nullptr);
		llvm_type = llvm_create_function_type(
				status, builder, args, return_type, *obj);
		assert(llvm_type->isFunctionTy());
	} else {
		assert(llvm_type != nullptr);
		assert_implies(llvm_type->isPointerTy(), term.is_pointer());
		assert(args.size() == 0);
	}

	if (!!status) {
		assert(obj != nullptr);

		/* we haven't seen this term before, let's create it */
		bound_type = bound_type_t::create(term, llvm_type, obj);

		assert_implies(term.is_function(),
			   	bound_type->llvm_type->isFunctionTy() ||
				(bound_type->llvm_type->isPointerTy() &&
				 bound_type->llvm_type->getPointerElementType()->isFunctionTy()));
		put_bound_type(term, bound_type);
		return bound_type;
	}

	assert(!status);
	return nullptr;
}
#endif

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

bool scope_t::has_bound_type(
		types::signature signature,
	   	resolution_constraints_t resolution_constraints)
{
	auto iter = bound_types.find(signature);
	if (iter != bound_types.end()) {
		/* we found this signature */
		return true;
	} else if (auto parent_scope = get_parent_scope()) {
		/* we did not find the signature, let's consider looking higher up the
		 * scopes */
		switch (resolution_constraints) {
		case rc_all_scopes:
			return parent_scope->has_bound_type(signature,
					resolution_constraints);
		case rc_just_current_scope:
			return false;
		case rc_capture_level:
			if (dynamic_cast<const function_scope_t *>(this)) {
				return false;
			} else {
				return parent_scope->has_bound_type(signature,
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

	debug_above(3, log(log_info, "no bound variable found for %s in\n%s", 
				obj->token.str().c_str(),
				str().c_str()));
	return nullptr;
}

types::term::ref scope_t::get_type_term(types::signature signature) {
	/* this function should only be called when we know that a type should exist */
	auto term = maybe_get_type_term(signature);

	if (term == nullptr) {
		log(log_error, "failed to find a bound type for " c_error("%s"),
				signature.str().c_str());
		dbg();
	}

	return term;
}

types::term::ref scope_t::maybe_get_type_term(types::signature signature) {
	/* get a type macro if it exists */
	auto iter = bound_types.find(signature);
	if (iter != bound_types.end()) {
		return iter->second->get_term();
	} else if (auto parent_scope = get_parent_scope()) {
		return parent_scope->get_type_term(signature);
	} else {
		return nullptr;
	}
}

#if 0
types::term::ref scope_t::rebind_type_name(
		status_t &status,
		const ast::item &obj,
		const types::term::ref &term,
		int *generic_index)
{
	/* the job of this function is to ensure that if this type is generic,
	 * we figure out whether the "any" types can resolve, and if so, can we 
	 * map those to names that are not generic. finally, once we've resolved
	 * the name, we need to find or create a bound type that represents this
	 * term */
	if (term.is_generic()) {
		atom name;
		if (term.is_unnamed_any()) {
			/* this type name is just "any". let's index it. */
			if (generic_index != nullptr) {
				name = get_indexed_generic(*generic_index);
			} else {
				user_error(status, obj, "unnamed \"any\" is out of place here. you must reference the parameter type");
			}
		} else {
			/* this is an already named "any" */
			name = term.name;
		}

		if (!!status) {
			if (name.is_generic_type_alias()) {
				/* this type operator is not resolved */
				if (term.args.size() == 0) {
					auto bound_type = maybe_get_bound_type(name);
					if (bound_type != nullptr) {
						/* this type operator name is already registered, let's replace our
						 * generic with what it resolved to */
						log(log_info, "resolved " c_type("%s") " to %s", name.c_str(), bound_type->str().c_str());
						return bound_type->term;
					} else {
						/* we couldn't find a type for the term name */
						user_error(status, obj, "unable to resolve type of " c_type("%s"), name.str().c_str());
					}
				} else {
					// TODO: handle generic type names with arguments
					user_error(status, obj, "not-impl: generic type names with arguments");
				}
			} else {
				/* the name of this type operator is not generic, let's check the
				 * arguments */
				types::term::refs args;
				for (auto &arg : term.args) {
					/* recurse to resolve type arguments */
					args.push_back(rebind_type_name(status, obj, arg, generic_index));
				}
				return make_term(term.name, args);
			}
		}
	} else {
		/* this term is ready to be resolved by type instantiation */
		return term;
	}

	assert(!status);
	return {""};
}
#endif

llvm::Module *scope_t::get_llvm_module() {
	if (get_parent_scope()) {
		return get_parent_scope()->get_llvm_module();
	} else {
		assert(false);
		return nullptr;
	}
}

std::string scope_t::make_fqn(std::string leaf_name) {
	return get_name() + SCOPE_SEP + leaf_name;
}

bound_type_t::ref scope_t::get_bound_type(types::signature signature) {
	return null_impl();
}

bool scope_t::put_bound_type(types::signature signature, bound_type_t::ref type) {
	not_impl();
	return false;
}

#if 0
ptr<module_scope_t> scope_t::get_module(atom symbol) const {
	if (auto parent_scope = get_parent_scope()) {
		return parent_scope->get_module(symbol);
	} else {
		return nullptr;
	}
}

ptr<module_scope_t> module_scope_t::get_module(atom symbol) const {
	/* module scope has linked modules that may be of use to us */
	auto iter = linked_modules.find(symbol);
	if (iter != linked_modules.end()) {
		return iter->second;
	} else {
		/* or not... */
		return parent_scope->get_module(symbol);
	}
}
#endif

function_scope_t::ref function_scope_t::create(atom module_name, scope_t::ref parent_scope) {
	return make_ptr<function_scope_t>(module_name, parent_scope);
}

ptr<scope_t> function_scope_t::get_parent_scope() {
	return parent_scope;
}

local_scope_t::ref local_scope_t::create(
		atom name,
		scope_t::ref parent_scope,
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
					dyncast<const ast::function_defn>(var->node));
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
	return local_scope_t::create(name, shared_from_this(), return_type_constraint);
}

ptr<local_scope_t> local_scope_t::new_local_scope(atom name) {
	return local_scope_t::create(name, shared_from_this(), return_type_constraint);
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

void runnable_scope_t::check_or_update_return_type_constraint(
		status_t &status,
		const ast::item::ref &return_statement,
		bound_type_t::ref return_type)
{
	return_type_constraint_t &return_type_constraint = get_return_type_constraint();
	if (return_type_constraint == nullptr) {
		return_type_constraint = return_type;
		log(log_info, "set return type to %s", return_type_constraint->str().c_str());
	} else if (return_type != return_type_constraint) {
		// TODO: consider directional unification here
		// TODO: consider storing more useful info in return_type_constraint
		user_error(status, *return_statement, "return expression type %s does not match %s",
				return_type->str().c_str(), return_type_constraint->str().c_str());
	} else {
		/* this return type checks out */
	}
}

local_scope_t::local_scope_t(
		atom name,
		scope_t::ref parent_scope,
		return_type_constraint_t &return_type_constraint)
: runnable_scope_t(name), parent_scope(parent_scope), return_type_constraint(return_type_constraint)
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
	dump_bindings(os, bound_vars, bound_types);
	dump_bindings(os, unchecked_vars, unchecked_types);
	// dump_linked_modules(os, linked_modules);
}

void function_scope_t::dump(std::ostream &os) const {
	os << std::endl << "FUNCTION SCOPE: " << name << std::endl;
	dump_bindings(os, bound_vars, bound_types);
}

void local_scope_t::dump(std::ostream &os) const {
	os << std::endl << "LOCAL SCOPE: " << name << std::endl;
	dump_bindings(os, bound_vars, bound_types);
}

void generic_substitution_scope_t::dump(std::ostream &os) const {
	os << std::endl << "GENERIC SUBSTITUTION SCOPE: " << name << std::endl;
	dump_bindings(os, bound_vars, bound_types);
}

module_scope_t::module_scope_t(atom name, program_scope_t::ref parent_scope, llvm::Module *llvm_module) :
	scope_t(name), parent_scope(parent_scope), llvm_module(llvm_module)
{
}

bool module_scope_t::has_checked(const ptr<const ast::item> &node) const {
	return visited.find(node) != visited.end();
}

void module_scope_t::mark_checked(const ptr<const ast::item> &node) {
	if (auto function_defn = dyncast<const ast::function_defn>(node)) {
		if (is_function_defn_generic(*function_defn)) {
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

unchecked_var_t::ref module_scope_t::put_unchecked_variable(
		atom symbol,
		unchecked_var_t::ref unchecked_variable)
{
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

#if 0
void module_scope_t::add_linked_module(
		status_t &status,
		ast::item::ref obj,
		atom symbol,
		module_scope_t::ref module_scope)
{
	if (linked_modules.find(symbol) != linked_modules.end()) {
		user_error(status, *obj, "module link already exists");
	} else {
		debug_above(3, log(log_info, "adding linked module " c_module("`%s`") " to module scope `%s`",
					symbol.str().c_str(),
					get_name().c_str()));
		linked_modules.insert({symbol, module_scope});
	}
}
#endif

ptr<scope_t> module_scope_t::get_parent_scope() {
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
	return make_ptr<program_scope_t>(name);
}

ptr<scope_t> generic_substitution_scope_t::get_parent_scope() {
	return parent_scope;
}

generic_substitution_scope_t::ref generic_substitution_scope_t::create(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ptr<const ast::item> &fn_decl,
		scope_t::ref parent_scope,
		const ptr<const unification_t> &unification)
{
	auto subst_scope = make_ptr<generic_substitution_scope_t>("generic substitution", parent_scope);
	for (auto &pair : unification->bindings) {
		auto bound_type = create_bound_type(
				status,
				builder,
				pair.second);
				
		if (!bound_type) {
			user_error(status, *fn_decl, "when trying to instantiate %s, couldn't find or create type %s",
					fn_decl->token.str().c_str(),
					pair.second->str().c_str());
			return nullptr;
		} else {
			/* the substitution scope allows us to masquerade a generic name as
			 * a bound type */
			subst_scope->put_bound_type(pair.first, bound_type);
		}
	}
	return subst_scope;
}

#if 0
generic_substitution_scope_t::ref generic_substitution_scope_t::create_for_types(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ptr<const ast::item> &fn_decl,
		scope_t::ref parent_scope,
		const ptr<const unification_t> &unification,
		const atom::set &type_variables)
{
	auto subst_scope = make_ptr<generic_substitution_scope_t>("generic substitution", parent_scope);
	for (auto &pair : unification->generics) {
		atom subst_type = pair.second->repr();
		auto type = parent_scope->get_bound_type(subst_type);
		if (!type) {
			user_error(status, *fn_decl, "when trying to instantiate %s, couldn't find type %s",
					fn_decl->token.str().c_str(),
					pair.second->str().c_str());
			return nullptr;
		} else {
			/* the substitution scope allows us to masquerade a generic name as
			 * a bound type */
			subst_scope->put_bound_type(dequantify_atom(
						pair.first, type_variables), type);
		}
	}
	return subst_scope;
}
#endif
