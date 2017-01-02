#include "zion.h"
#include "logger.h"
#include "dbg.h"
#include "scopes.h"
#include "ast.h"
#include "utils.h"
#include "llvm_utils.h"
#include "llvm_types.h"
#include "unification.h"

bound_var_t::ref get_bound_variable_from_scope(
		status_t &status,
		const ptr<const ast::item> &obj,
		atom symbol,
		bound_var_t::map bound_vars,
		scope_t::ref parent_scope)
{
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
	} else if (parent_scope != nullptr) {
		return parent_scope->get_bound_variable(status, obj, symbol);
	}

	debug_above(3, log(log_info, "no bound variable found when resolving %s (looking for " c_id("%s"), 
				obj->token.str().c_str(),
				symbol.c_str()));
	return nullptr;
}

bound_type_t::ref get_bound_type_from_scope(
		types::signature signature,
		std::string fqn_signature,
		program_scope_t::ref program_scope,
	   	scope_t::ref parent_scope)
{
	indent_logger indent(9, string_format("checking whether %s is bound...",
				signature.str().c_str()));
	auto full_signature = types::signature{fqn_signature};
	auto bound_type = program_scope->get_bound_type(full_signature);
	if (bound_type != nullptr) {
		debug_above(9, log(log_info, "yep. %s is bound to %s",
					signature.str().c_str(),
					bound_type->str().c_str()));
		return bound_type;
	} else {
		debug_above(9, log(log_info, "nope. %s is not yet bound",
					signature.str().c_str()));
		return parent_scope->get_bound_type(signature);
	}
}

std::string scope_t::get_name() const {
	auto parent_scope = this->get_parent_scope();
	if (parent_scope) {
		return parent_scope->get_name() + SCOPE_SEP + get_leaf_name().str();
	} else {
		return get_leaf_name().str();
	}
}

ptr<program_scope_t> program_scope_t::get_program_scope() {
	return std::static_pointer_cast<program_scope_t>(shared_from_this());
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

llvm::Module *scope_t::get_llvm_module() {
	if (get_parent_scope()) {
		return get_parent_scope()->get_llvm_module();
	} else {
		assert(false);
		return nullptr;
	}
}

bound_type_t::ref program_scope_t::get_bound_type(types::signature signature) {
	indent_logger indent(9, string_format("checking program scope whether %s is bound...",
				signature.str().c_str()));
	auto iter = bound_types.find(signature);
	if (iter != bound_types.end()) {
		debug_above(9, log(log_info, "yep. %s is bound to %s",
					signature.str().c_str(),
					iter->second->str().c_str()));
		return iter->second;
	} else {
		debug_above(9, log(log_info, "nope. %s is not yet bound",
					signature.str().c_str()));
		return nullptr;
	}
}

function_scope_t::ref function_scope_t::create(atom module_name, scope_t::ref parent_scope) {
	return make_ptr<function_scope_t>(module_name, parent_scope);
}

local_scope_t::ref local_scope_t::create(
		atom name,
		scope_t::ref parent_scope,
		return_type_constraint_t &return_type_constraint)
{
	return make_ptr<local_scope_t>(name, parent_scope, return_type_constraint);
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
			assert(dyncast<const ast::function_defn>(var->node) ||
					dyncast<const ast::type_product>(var->node));
			fns.push_back(var);
		}
	}
}

void module_scope_impl_t::get_callables(atom symbol, var_t::refs &fns) {
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

runnable_scope_t::runnable_scope_t(
        atom name,
		scope_t::ref parent_scope) : scope_impl_t(name, parent_scope)
{
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
				return_type_constraint->get_type(),
				return_type->get_type(),
				get_typename_env(),
				get_type_variable_bindings());

		if (!!status) {
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
}

local_scope_t::local_scope_t(
		atom name,
		scope_t::ref parent_scope,
		return_type_constraint_t &return_type_constraint) :
   	runnable_scope_t(name, parent_scope),
   	return_type_constraint(return_type_constraint)
{
}

void dump_bindings(
		std::ostream &os,
		const bound_var_t::map &bound_vars,
		const bound_type_t::map &bound_types)
{
	if (bound_vars.size() != 0) {
		os << "bound vars:\n";
		for (auto &var_pair : bound_vars) {
			os << C_VAR << var_pair.first << C_RESET << ": ";
			const auto &overloads = var_pair.second;
			os << ::str(overloads);
		}
	}

	if (bound_types.size() != 0) {
		os << "bound types:\n";
		for (auto &type_pair : bound_types) {
			os << C_TYPE << type_pair.first << C_RESET << ": ";
			os << *type_pair.second << std::endl;
		}
	}
}

void dump_bindings(
		std::ostream &os,
		const unchecked_var_t::map &unchecked_vars,
		const unchecked_type_t::map &unchecked_types)
{
	if (unchecked_vars.size() != 0) {
		os << "unchecked vars:\n";
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
	}

	if (unchecked_types.size() != 0) {
		os << "unchecked types:\n";
		for (auto &type_pair : unchecked_types) {
			os << C_TYPE << type_pair.first << C_RESET << ": ";
			os << type_pair.second->node->token.str() << std::endl;
		}
	}
}

void dump_linked_modules(std::ostream &os, const module_scope_t::map &modules) {
	os << "modules: " << str(modules) << std::endl;
}

void dump_type_map(std::ostream &os, types::type::map env, std::string desc) {
	if (env.size() != 0) {
		os << std::endl << desc << std::endl;
		os << join_with(env, "\n", [] (types::type::map::value_type value) -> std::string {
			return string_format("%s: %s", value.first.c_str(), value.second->str().c_str());
		});
		os << std::endl;
	}
}

void program_scope_t::dump(std::ostream &os) const {
	os << std::endl << "PROGRAM SCOPE: " << name << std::endl;
	dump_bindings(os, bound_vars, bound_types);
	dump_bindings(os, unchecked_vars, unchecked_types);
	dump_type_map(os, typename_env, "PROGRAM TYPENAME ENV");
	dump_type_map(os, type_variable_bindings, "PROGRAM TYPE VARIABLE BINDINGS");
}

void module_scope_impl_t::dump(std::ostream &os) const {
	os << std::endl << "MODULE SCOPE: " << name << std::endl;
	dump_bindings(os, bound_vars, {});
	dump_bindings(os, unchecked_vars, unchecked_types);
	dump_type_map(os, typename_env, "MODULE TYPENAME ENV");
	dump_type_map(os, type_variable_bindings, "MODULE TYPE VARIABLE BINDINGS");
	get_parent_scope()->dump(os);
}

void function_scope_t::dump(std::ostream &os) const {
	os << std::endl << "FUNCTION SCOPE: " << name << std::endl;
	dump_bindings(os, bound_vars, {});
	dump_type_map(os, typename_env, "FUNCTION TYPENAME ENV");
	dump_type_map(os, type_variable_bindings, "FUNCTION TYPE VARIABLE BINDINGS");
	get_parent_scope()->dump(os);
}

void local_scope_t::dump(std::ostream &os) const {
	os << std::endl << "LOCAL SCOPE: " << name << std::endl;
	dump_bindings(os, bound_vars, {});
	dump_type_map(os, typename_env, "LOCAL TYPENAME ENV");
	dump_type_map(os, type_variable_bindings, "LOCAL TYPE VARIABLE BINDINGS");
	get_parent_scope()->dump(os);
}

generic_substitution_scope_t::generic_substitution_scope_t(
        atom name,
        scope_t::ref parent_scope,
        types::type::ref callee_signature) :
    scope_impl_t(name, parent_scope), callee_signature(callee_signature)
{
}

void generic_substitution_scope_t::dump(std::ostream &os) const {
	os << std::endl << "GENERIC SUBSTITUTION SCOPE: " << name << std::endl;
	os << "For Callee Signature: " << callee_signature->str() << std::endl;
	dump_bindings(os, bound_vars, {});
	dump_type_map(os, typename_env, "GENERIC SUBSTITUTION TYPENAME ENV");
	dump_type_map(os, type_variable_bindings, "GENERIC SUBSTITUTION TYPE VARIABLE BINDINGS");
	get_parent_scope()->dump(os);
}

module_scope_impl_t::module_scope_impl_t(
		atom name,
	   	program_scope_t::ref parent_scope,
		llvm::Module *llvm_module) :
	scope_impl_t<module_scope_t>(name, parent_scope),
   	llvm_module(llvm_module)
{
}

bool module_scope_impl_t::has_checked(const ptr<const ast::item> &node) const {
	return visited.find(node) != visited.end();
}

void module_scope_impl_t::mark_checked(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	const ptr<const ast::item> &node) {
	if (auto function_defn = dyncast<const ast::function_defn>(node)) {
		if (is_function_defn_generic(status, builder, shared_from_this(),
					*function_defn)) {
			/* for now let's never mark generic functions as checked, until we
			 * have a mechanism to join the type to the checked-mark.  */
			return;
		}
	}

	assert(!has_checked(node));
	visited.insert(node);
}

void module_scope_impl_t::put_unchecked_type(
		status_t &status,
		unchecked_type_t::ref unchecked_type)
{
	debug_above(6, log(log_info, "registering an unchecked type %s as %s",
				unchecked_type->str().c_str()));

	auto unchecked_type_iter = unchecked_types.find(unchecked_type->name);

	if (unchecked_type_iter == unchecked_types.end()) {
		unchecked_types.insert({unchecked_type->name, unchecked_type});

		/* also keep an ordered list of the unchecked types */
		unchecked_types_ordered.push_back(unchecked_type);
	} else {
		/* this unchecked type already exists */
		user_error(status, *unchecked_type->node, "type " c_type("%s") " already exists",
				unchecked_type->name.c_str());

		user_error(status, *unchecked_type_iter->second->node,
				"see type " c_type("%s") " declaration",
				unchecked_type_iter->second->name.c_str());
	}
}

unchecked_type_t::ref module_scope_impl_t::get_unchecked_type(atom symbol) {
	auto iter = unchecked_types.find(symbol);
	if (iter != unchecked_types.end()) {
		return iter->second;
	} else {
		return nullptr;
	}
}

unchecked_type_t::refs &module_scope_impl_t::get_unchecked_types_ordered() {
	return unchecked_types_ordered;
}

unchecked_var_t::refs &module_scope_impl_t::get_unchecked_vars_ordered() {
	return unchecked_vars_ordered;
}

unchecked_var_t::ref put_unchecked_variable_impl(
		atom symbol,
		unchecked_var_t::ref unchecked_variable,
		unchecked_var_t::map &unchecked_vars,
		unchecked_var_t::refs &unchecked_vars_ordered,
		std::string current_scope_name,
		program_scope_t::ref program_scope)
{
	debug_above(6, log(log_info, "registering an unchecked variable %s as %s in " c_id("%s"),
				symbol.c_str(),
				unchecked_variable->str().c_str(),
                current_scope_name.c_str()));

	auto iter = unchecked_vars.find(symbol);
	if (iter != unchecked_vars.end()) {
		/* this variable already exists, let's consider overloading it */
		if (dyncast<const ast::function_defn>(unchecked_variable->node)) {
			iter->second.push_back(unchecked_variable);
		} else if (dyncast<const unchecked_data_ctor_t>(unchecked_variable)) {
			iter->second.push_back(unchecked_variable);
		} else {
			dbg();
			assert(!"why are we putting this here?");
		}
	} else {
		unchecked_vars[symbol] = {unchecked_variable};
	}

	/* also keep a list of the order in which we encountered these */
	unchecked_vars_ordered.push_back(unchecked_variable);

#if 0
	// TODO: consider using module variables for type dereferencing
	if (program_scope) {
		program_scope->put_unchecked_variable(
				current_scope_name + SCOPE_SEP + symbol.str(),
				unchecked_variable);	
	}
#endif

	return unchecked_variable;
}

unchecked_var_t::ref module_scope_impl_t::put_unchecked_variable(
		atom symbol,
		unchecked_var_t::ref unchecked_variable)
{
	return put_unchecked_variable_impl(symbol, unchecked_variable,
			unchecked_vars, unchecked_vars_ordered, get_name(),
			get_program_scope());
}

unchecked_var_t::ref program_scope_t::put_unchecked_variable(
		atom symbol,
	   	unchecked_var_t::ref unchecked_variable)
{
	return put_unchecked_variable_impl(symbol, unchecked_variable,
			unchecked_vars, unchecked_vars_ordered, get_name(), nullptr);
}

void program_scope_t::put_bound_type(status_t &status, bound_type_t::ref type) {
	debug_above(8, log(log_info, "binding type %s as " c_id("%s"),
				type->str().c_str(),
				type->get_signature().repr().c_str()));
	atom signature = type->get_signature().repr();
	auto iter = bound_types.find(signature);
	if (iter == bound_types.end()) {
		bound_types[signature] = type;
	} else {
		/* this type symbol already exists */
		if (auto handle = dyncast<const bound_type_handle_t>(iter->second)) {
			handle->set_actual(type);
		} else {
			user_error(status, type->get_location(), "type %s already exists",
					type->str().c_str());
			user_error(status, iter->second->get_location(), "type %s was declared here",
					iter->second->str().c_str());
		}
	}
}

ptr<module_scope_t> program_scope_t::new_module_scope(
		atom name,
		llvm::Module *llvm_module)
{
	assert(!lookup_module(name));
	auto module_scope = module_scope_impl_t::create(name, get_program_scope(), llvm_module);
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
		ss << llvm_print_module(*module_pair.second->get_llvm_module());
	}
	return ss.str();
}

module_scope_t::ref module_scope_impl_t::create(
		atom name,
		program_scope_t::ref parent_scope,
		llvm::Module *llvm_module)
{
	return make_ptr<module_scope_impl_t>(name, parent_scope, llvm_module);
}

llvm::Module *module_scope_impl_t::get_llvm_module() {
	return llvm_module;
}

llvm::Module *generic_substitution_scope_t::get_llvm_module() {
	return get_parent_scope()->get_llvm_module();
}

program_scope_t::ref program_scope_t::create(atom name, llvm::Module *llvm_module) {
	return make_ptr<program_scope_t>(name, llvm_module);
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
			subst_scope->put_type_variable_binding(status, pair.first, pair.second);
			if (!status) {
				break;
			}
		} else {
			debug_above(7, log(log_info, "skipping adding %s to generic substitution scope",
						pair.first.c_str()));
		}
	}

	if (!!status) {
		return subst_scope;
	} else {
		return nullptr;
	}
}
