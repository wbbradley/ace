#include "zion.h"
#include "logger.h"
#include "dbg.h"
#include "scopes.h"
#include "ast.h"
#include "utils.h"
#include "llvm_utils.h"
#include "llvm_types.h"
#include "unification.h"
#include "compiler.h"

const char *GLOBAL_ID = "_";
const token_kind SCOPE_TK = tk_dot;
const char SCOPE_SEP_CHAR = '.';
const char *SCOPE_SEP = ".";

void resolve_unchecked_type(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	module_scope_t::ref module_scope,
	   	unchecked_type_t::ref unchecked_type);

bound_var_t::ref get_bound_variable_from_scope(
		status_t &status,
		location_t location,
		atom scope_name,
		atom symbol,
		const bound_var_t::map &bound_vars,
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
			user_error(status, location,
				   	"a non-callsite reference to an overloaded variable " c_id("%s") " was found. overloads at this immediate location are:\n%s",
					symbol.c_str(),
					::str(overloads).c_str());
			return nullptr;
		}
	} else if (parent_scope != nullptr) {
		return parent_scope->get_bound_variable(status, location, symbol);
	}

	debug_above(6, log(log_info,
			   	"no bound variable found when looking for " c_id("%s") " in " c_id("%s"), 
				symbol.c_str(),
				scope_name.c_str()));
	return nullptr;
}

bound_type_t::ref get_bound_type_from_scope(
		types::signature signature,
		program_scope_t::ref program_scope, bool use_mappings)
{
	INDENT(9, string_format("checking whether %s is bound...",
				signature.str().c_str()));
	auto bound_type = program_scope->get_bound_type(signature, use_mappings);
	if (bound_type != nullptr) {
		debug_above(9, log(log_info, c_good("yep") ". %s is bound to %s",
					signature.str().c_str(),
					bound_type->str().c_str()));
		return bound_type;
	} else {
		debug_above(9, log(log_info, c_warn("nope") ". %s is not yet bound",
					signature.str().c_str()));
		return nullptr;
	}
}

std::string scope_t::get_name() const {
	auto parent_scope = this->get_parent_scope();
	if (parent_scope && !dyncast<const program_scope_t>(parent_scope)) {
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

ptr<const module_scope_t> scope_t::get_module_scope() const {
	if (auto module_scope = dyncast<const module_scope_t>(shared_from_this())) {
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

bound_type_t::ref program_scope_t::get_bound_type(types::signature signature, bool use_mappings) {
	INDENT(9, string_format("checking program scope whether %s is bound...",
				signature.str().c_str()));
	auto iter = bound_types.find(signature);
	if (iter != bound_types.end()) {
		debug_above(9, log(log_info, "yep. %s is bound to %s",
					signature.str().c_str(),
					iter->second->str().c_str()));
		return iter->second;
	} else if (use_mappings) {
		auto dest_iter = bound_type_mappings.find(signature);
		if (dest_iter != bound_type_mappings.end()) {
			debug_above(4, log("falling back to bound type mappings to find %s (resolved to %s)",
					signature.str().c_str(),
					dest_iter->second.str().c_str()));
			return get_bound_type(dest_iter->second);
		}
	}

	debug_above(9, log(log_info, "nope. %s is not yet bound",
				signature.str().c_str()));
	return nullptr;
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
			assert(dyncast<const ast::function_defn_t>(var->node) ||
					dyncast<const ast::var_decl_t>(var->node) ||
					dyncast<const ast::type_product_t>(var->node) ||
					dyncast<const ast::link_function_statement_t>(var->node));
			fns.push_back(var);
		}
	}
}

bound_type_t::ref program_scope_t::get_runtime_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		std::string name,
		bool get_ptr)
{
	module_scope_t::ref runtime_module = lookup_module("runtime");
	if (runtime_module != nullptr) {
		auto type = type_id(make_iid_impl(std::string("runtime.") + name, INTERNAL_LOC()));
		if (get_ptr) {
			type = type_ptr(type);
		}
		return upsert_bound_type(status, builder, runtime_module, type);
	} else {
		user_error(status, INTERNAL_LOC(), c_id("runtime") " module is not yet installed.");
	}

	assert(!status);
	return nullptr;
}

void program_scope_t::get_callables(atom symbol, var_t::refs &fns) {
	get_callables_from_bound_vars(symbol, bound_vars, fns);
	get_callables_from_unchecked_vars(symbol, unchecked_vars, fns);
}

llvm::Type *program_scope_t::get_llvm_type(status_t &status, location_t location, std::string type_name) {
	for (auto &module_pair : compiler.llvm_modules) {
		debug_above(4, log("looking for type " c_type("%s") " in module " C_FILENAME "%s" C_RESET,
					type_name.c_str(),
					module_pair.first.c_str()));
		auto &llvm_module = module_pair.second;
		llvm::Type *llvm_type = llvm_module->getTypeByName(type_name);
		if (llvm_type != nullptr) {
			return llvm_type;
		}
	}

	user_error(status, location, "couldn't find type " c_type("%s"), type_name.c_str());
	return nullptr;
}

llvm::Function *program_scope_t::get_llvm_function(status_t &status, location_t location, std::string function_name) {
	for (auto &module_pair : compiler.llvm_modules) {
		debug_above(4, log("looking for function " c_var("%s") " in module " C_FILENAME "%s" C_RESET,
					function_name.c_str(),
					module_pair.first.c_str()));
		auto &llvm_module = module_pair.second;
		llvm::Function *llvm_function = llvm_module->getFunction(function_name);
		if (llvm_function != nullptr) {
			return llvm_function;
		}
	}

	user_error(status, location, "couldn't find function " c_var("%s"), function_name.c_str());
	return nullptr;
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
		const ast::item_t::ref &return_statement,
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
				user_error(status, *return_statement,
						"return expression type %s does not match %s",
						return_type->get_type()->str().c_str(),
						return_type_constraint->get_type()->str().c_str());
			} else {
				/* this return type checks out */
				debug_above(2, log(log_info, "unified %s :> %s",
							return_type_constraint->str().c_str(),
							return_type->str().c_str()));
			}
		}
	}
}

void runnable_scope_t::set_innermost_loop_bbs(llvm::BasicBlock *new_loop_continue_bb, llvm::BasicBlock *new_loop_break_bb) {
	assert(new_loop_continue_bb != loop_continue_bb);
	assert(new_loop_break_bb != loop_break_bb);

	loop_continue_bb = new_loop_continue_bb;
	loop_break_bb = new_loop_break_bb;
}

llvm::BasicBlock *runnable_scope_t::get_innermost_loop_break() const {
	/* regular scopes (not runnable scope) doesn't have the concept of loop
	 * exits */
	if (loop_break_bb == nullptr) {
		if (auto parent_scope = dyncast<const runnable_scope_t>(get_parent_scope())) {
			return parent_scope->get_innermost_loop_break();
		} else {
			return nullptr;
		}
	} else {
		return loop_break_bb;
	}
}

llvm::BasicBlock *runnable_scope_t::get_innermost_loop_continue() const {
	/* regular scopes (not runnable scope) doesn't have the concept of loop
	 * exits */
	if (loop_continue_bb == nullptr) {
		if (auto parent_scope = dyncast<const runnable_scope_t>(get_parent_scope())) {
			return parent_scope->get_innermost_loop_continue();
		} else {
			return nullptr;
		}
	} else {
		return loop_continue_bb;
	}
}

loop_tracker_t::loop_tracker_t(
		runnable_scope_t::ref scope,
	   	llvm::BasicBlock *loop_continue_bb,
	   	llvm::BasicBlock *loop_break_bb) :
	scope(scope),
   	prior_loop_continue_bb(scope->get_innermost_loop_continue()),
   	prior_loop_break_bb(scope->get_innermost_loop_break())
{
	assert(scope != nullptr);
	assert(loop_continue_bb != nullptr);
	assert(loop_break_bb != nullptr);

	scope->set_innermost_loop_bbs(loop_continue_bb, loop_break_bb);
}

loop_tracker_t::~loop_tracker_t() {
	scope->set_innermost_loop_bbs(prior_loop_continue_bb, prior_loop_break_bb);
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
		const unchecked_var_t::map &unchecked_vars)
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
}

void dump_bindings(
		std::ostream &os,
		const unchecked_type_t::map &unchecked_types)
{
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

void dump_type_map(std::ostream &os, types::type_t::map env, std::string desc) {
	if (env.size() != 0) {
		os << std::endl << desc << std::endl;
		os << join_with(env, "\n", [] (types::type_t::map::value_type value) -> std::string {
			return string_format("%s: %s", value.first.c_str(), value.second->str().c_str());
		});
		os << std::endl;
	}
}

void program_scope_t::dump(std::ostream &os) const {
	os << std::endl << "PROGRAM SCOPE: " << scope_name << std::endl;
	dump_bindings(os, bound_vars, bound_types);
	dump_bindings(os, unchecked_vars);
	dump_bindings(os, unchecked_types);
	dump_type_map(os, typename_env, "PROGRAM TYPENAME ENV");
	dump_type_map(os, type_variable_bindings, "PROGRAM TYPE VARIABLE BINDINGS");
}

void module_scope_impl_t::dump(std::ostream &os) const {
	os << std::endl << "MODULE SCOPE: " << scope_name << std::endl;
	dump_bindings(os, bound_vars, {});
	dump_bindings(os, unchecked_types);
	dump_type_map(os, typename_env, "MODULE TYPENAME ENV");
	dump_type_map(os, type_variable_bindings, "MODULE TYPE VARIABLE BINDINGS");
	get_parent_scope()->dump(os);
}

void function_scope_t::dump(std::ostream &os) const {
	os << std::endl << "FUNCTION SCOPE: " << scope_name << std::endl;
	dump_bindings(os, bound_vars, {});
	dump_type_map(os, typename_env, "FUNCTION TYPENAME ENV");
	dump_type_map(os, type_variable_bindings, "FUNCTION TYPE VARIABLE BINDINGS");
	get_parent_scope()->dump(os);
}

void local_scope_t::dump(std::ostream &os) const {
	os << std::endl << "LOCAL SCOPE: " << scope_name << std::endl;
	dump_bindings(os, bound_vars, {});
	dump_type_map(os, typename_env, "LOCAL TYPENAME ENV");
	dump_type_map(os, type_variable_bindings, "LOCAL TYPE VARIABLE BINDINGS");
	get_parent_scope()->dump(os);
}

generic_substitution_scope_t::generic_substitution_scope_t(
        atom name,
        scope_t::ref parent_scope,
        types::type_t::ref callee_signature) :
    scope_impl_t(name, parent_scope), callee_signature(callee_signature)
{
}

void generic_substitution_scope_t::dump(std::ostream &os) const {
	os << std::endl << "GENERIC SUBSTITUTION SCOPE: " << scope_name << std::endl;
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

bool module_scope_impl_t::has_checked(const ptr<const ast::item_t> &node) const {
	return visited.find(node) != visited.end();
}

bool module_scope_impl_t::symbol_exists_in_running_scope(atom symbol, bound_var_t::ref &bound_var) {
	return false;
}

void module_scope_impl_t::mark_checked(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	const ptr<const ast::item_t> &node) {
	if (auto function_defn = dyncast<const ast::function_defn_t>(node)) {
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

std::string module_scope_impl_t::make_fqn(std::string leaf_name) const {
	assert(leaf_name.find(SCOPE_SEP) == std::string::npos);
	return get_leaf_name().str() + SCOPE_SEP + leaf_name;
}

void module_scope_impl_t::put_unchecked_type(
		status_t &status,
		unchecked_type_t::ref unchecked_type)
{
	// assert(unchecked_type->name.str().find(SCOPE_SEP) != std::string::npos);
	debug_above(6, log(log_info, "registering an unchecked type " c_type("%s") " %s in scope " c_id("%s"),
				unchecked_type->name.c_str(),
				unchecked_type->str().c_str(),
				get_name().c_str()));

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

bound_var_t::ref program_scope_t::upsert_init_module_vars_function(
		status_t &status,
	   	llvm::IRBuilder<> &builder)
{
	if (init_module_vars_function != nullptr) {
		return init_module_vars_function;
	}

	/* build the global __init_module_vars function */
	llvm::IRBuilderBase::InsertPointGuard ipg(builder);

	/* we are creating this function, but we'll be adding to it elsewhere */
	init_module_vars_function = llvm_start_function(
			status,
			builder, 
			shared_from_this(),
			INTERNAL_LOC(),
			{},
			get_bound_type({"void"}),
			"__init_module_vars");

	if (!!status) {
		builder.CreateRetVoid();

		put_bound_variable(status, "__init_module_vars", init_module_vars_function);

		if (!!status) {
			return init_module_vars_function;
		}
	}

	assert(!status);
	return nullptr;
}

std::string program_scope_t::make_fqn(std::string name) const {
	return name;
}

void program_scope_t::set_insert_point_to_init_module_vars_function(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	std::string for_var_decl_name)
{
	auto fn = upsert_init_module_vars_function(status, builder);
	llvm::Function *llvm_function = llvm::dyn_cast<llvm::Function>(fn->get_llvm_value());
	assert(llvm_function != nullptr);

	builder.SetInsertPoint(&llvm_function->getEntryBlock(),
			llvm_function->getEntryBlock().getFirstInsertionPt());
}

unchecked_var_t::refs &program_scope_t::get_unchecked_vars_ordered() {
	return unchecked_vars_ordered;
}

unchecked_var_t::ref put_unchecked_variable_impl(
		atom symbol,
		unchecked_var_t::ref unchecked_variable,
		unchecked_var_t::map &unchecked_vars,
		unchecked_var_t::refs &unchecked_vars_ordered)
{
	debug_above(6, log(log_info,
			   	"registering an unchecked variable " c_id("%s") " as %s",
				symbol.c_str(),
				unchecked_variable->str().c_str()));

	auto iter = unchecked_vars.find(symbol);
	if (iter != unchecked_vars.end()) {
		/* this variable already exists, let's consider overloading it */
		if (dyncast<const ast::function_defn_t>(unchecked_variable->node)) {
			iter->second.push_back(unchecked_variable);
		} else if (dyncast<const unchecked_data_ctor_t>(unchecked_variable)) {
			iter->second.push_back(unchecked_variable);
		} else if (dyncast<const ast::var_decl_t>(unchecked_variable)) {
			iter->second.push_back(unchecked_variable);
		} else {
			dbg();
			panic("why are we putting this here?");
		}
	} else {
		unchecked_vars[symbol] = {unchecked_variable};
	}

	/* also keep a list of the order in which we encountered these */
	unchecked_vars_ordered.push_back(unchecked_variable);

	return unchecked_variable;
}

unchecked_var_t::ref program_scope_t::put_unchecked_variable(
		atom symbol,
	   	unchecked_var_t::ref unchecked_variable)
{
	return put_unchecked_variable_impl(symbol, unchecked_variable,
			unchecked_vars, unchecked_vars_ordered);
}

unchecked_var_t::ref module_scope_t::put_unchecked_variable(
		atom symbol,
	   	unchecked_var_t::ref unchecked_variable)
{
	return get_program_scope()->put_unchecked_variable(
			make_fqn(symbol.str()),
		   	unchecked_variable);
}

unchecked_var_t::ref program_scope_t::get_unchecked_variable(atom symbol) {
	debug_above(7, log("looking for unchecked variable " c_id("%s"), symbol.c_str()));
	var_t::refs vars;
	get_callables_from_unchecked_vars(
			symbol,
			unchecked_vars,
			vars);
	if (vars.size() != 1) {
		return nullptr;
	}
	return dyncast<const unchecked_var_t>(vars.front());
}

void program_scope_t::put_bound_type_mapping(
		status_t &status,
	   	types::signature source,
	   	types::signature dest)
{
	auto dest_iter = bound_type_mappings.find(source);
	if (dest_iter == bound_type_mappings.end()) {
		bound_type_mappings.insert({source, dest});
	} else {
		user_error(status, INTERNAL_LOC(), "bound type mapping %s already exists!",
				source.str().c_str());
	}
}

void program_scope_t::put_bound_type(status_t &status, bound_type_t::ref type) {
	debug_above(5, log(log_info, "binding type %s as " c_id("%s"),
				type->str().c_str(),
				type->get_signature().repr().c_str()));
	/*
	if (type->str().find("type_info_mark_fn_t") != std::string::npos) {
		dbg();
	}
	*/
	atom signature = type->get_signature().repr();
	auto iter = bound_types.find(signature);
	if (iter == bound_types.end()) {
		bound_types[signature] = type;
	} else {
		/* this type symbol already exists */
		user_error(status, type->get_location(), "type %s already exists",
				type->str().c_str());
		user_error(status, iter->second->get_location(), "type %s was declared here",
				iter->second->str().c_str());
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
	debug_above(8, log("looking for module %s in [%s]",
				symbol.c_str(),
				join_with(modules, ", ", [] (module_scope_t::map::value_type module) -> std::string {
					return module.first.c_str();
				}).c_str()));
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

program_scope_t::ref program_scope_t::create(atom name, compiler_t &compiler, llvm::Module *llvm_module) {
	return make_ptr<program_scope_t>(name, compiler, llvm_module);
}

generic_substitution_scope_t::ref generic_substitution_scope_t::create(
		status_t &status,
		llvm::IRBuilder<> &builder,
		const ptr<const ast::item_t> &fn_decl,
		scope_t::ref parent_scope,
		unification_t unification,
		types::type_t::ref callee_type)
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
