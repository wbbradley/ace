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
#include "disk.h"
#include <unistd.h>


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
		std::string scope_name,
		std::string symbol,
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
		return parent_scope->get_name() + SCOPE_SEP + get_leaf_name();
	} else {
		return get_leaf_name();
	}
}

ptr<program_scope_t> program_scope_t::get_program_scope() {
	return std::static_pointer_cast<program_scope_t>(shared_from_this());
}

ptr<const program_scope_t> program_scope_t::get_program_scope() const {
	return std::static_pointer_cast<const program_scope_t>(shared_from_this());
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

function_scope_t::ref function_scope_t::create(std::string module_name, scope_t::ref parent_scope) {
	return make_ptr<function_scope_t>(module_name, parent_scope);
}

local_scope_t::ref local_scope_t::create(
		std::string name,
		scope_t::ref parent_scope,
		return_type_constraint_t &return_type_constraint)
{
	return make_ptr<local_scope_t>(name, parent_scope, return_type_constraint);
}

void get_callables_from_bound_vars(
		std::string symbol,
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
		std::string symbol,
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
	auto type = type_id(make_iid_impl(name, INTERNAL_LOC()));
	if (get_ptr) {
		type = type_ptr(type);
	}
	return upsert_bound_type(status, builder, shared_from_this(), type);
}

void program_scope_t::get_callables(std::string symbol, var_t::refs &fns, bool check_unchecked) {
	get_callables_from_bound_vars(symbol, bound_vars, fns);
	if (check_unchecked) {
		get_callables_from_unchecked_vars(symbol, unchecked_vars, fns);
	}
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

ptr<local_scope_t> function_scope_t::new_local_scope(std::string name) {
	return local_scope_t::create(name, shared_from_this(), return_type_constraint);
}

ptr<local_scope_t> local_scope_t::new_local_scope(std::string name) {
	return local_scope_t::create(name, shared_from_this(), return_type_constraint);
}

return_type_constraint_t &function_scope_t::get_return_type_constraint() {
	return return_type_constraint;
}

return_type_constraint_t &local_scope_t::get_return_type_constraint() {
	return return_type_constraint;
}

runnable_scope_t::runnable_scope_t(
        std::string name,
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
		debug_above(5, log(log_info, "checking return type %s against %s",
					return_type->str().c_str(),
				   	return_type_constraint->str().c_str()));

		unification_t unification = unify(
				return_type_constraint->get_type(),
				return_type->get_type(),
				get_nominal_env(),
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
		std::string name,
		scope_t::ref parent_scope,
		return_type_constraint_t &return_type_constraint) :
   	runnable_scope_t(name, parent_scope),
   	return_type_constraint(return_type_constraint)
{
}

void dump_bindings(
		std::ostream &os,
		const bound_var_t::map &bound_vars,
		const bound_type_t::map &bound_types,
		bool tags_fmt=false)
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

void dump_unchecked_vars(
		std::ostream &os,
		const unchecked_var_t::map &unchecked_vars,
		bool tags_fmt=false)
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

void dump_unchecked_types(std::ostream &os, const unchecked_type_t::map &unchecked_types) {
	if (unchecked_types.size() != 0) {
		os << "unchecked types:\n";
		for (auto &type_pair : unchecked_types) {
			os << C_TYPE << type_pair.first << C_RESET << ": ";
			os << type_pair.second->node->token.str() << std::endl;
		}
	}
}

void dump_unchecked_type_tags(std::ostream &os, const unchecked_type_t::map &unchecked_types) {
	for (auto &type_pair : unchecked_types) {
		auto loc = type_pair.second->node->get_location();
		os << type_pair.first << "\t" << loc.filename_repr() << "\t" << loc.line << ";/^type " << type_pair.first << "/;\"\tkind:t" << std::endl;
	}
}

void dump_unchecked_var_tags(std::ostream &os, const unchecked_var_t::map &unchecked_vars) {
	for (auto &var_pair : unchecked_vars) {
		for (auto unchecked_var : var_pair.second) {
			auto loc = unchecked_var->node->get_location();
			os << var_pair.first << "\t" << loc.filename_repr() << "\t" << loc.line << ";/^\\(var\\|let\\|def\\) " << var_pair.first << "/;\"\tkind:f" << std::endl;
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

void module_scope_impl_t::dump_tags(std::ostream &os) const {
	// dump_unchecked_var_tags(os, unchecked_vars);
}
void program_scope_t::dump_tags(std::ostream &os) const {
	dump_unchecked_var_tags(os, unchecked_vars);
	dump_unchecked_type_tags(os, unchecked_types);
}

void program_scope_t::dump(std::ostream &os) const {
	os << std::endl << "PROGRAM SCOPE: " << scope_name << std::endl;
	dump_bindings(os, bound_vars, bound_types);
	dump_unchecked_vars(os, unchecked_vars);
	dump_unchecked_types(os, unchecked_types);
	dump_type_map(os, nominal_env, "PROGRAM NOMINAL ENV");
	dump_type_map(os, structural_env, "PROGRAM STRUCTURAL ENV");
	dump_type_map(os, type_variable_bindings, "PROGRAM TYPE VARIABLE BINDINGS");
}

void module_scope_impl_t::dump(std::ostream &os) const {
	os << std::endl << "MODULE SCOPE: " << scope_name << std::endl;
	dump_bindings(os, bound_vars, {});
	dump_unchecked_types(os, unchecked_types);
	dump_type_map(os, nominal_env, "MODULE NOMINAL ENV");
	dump_type_map(os, structural_env, "MODULE STRUCTURAL ENV");
	dump_type_map(os, type_variable_bindings, "MODULE TYPE VARIABLE BINDINGS");
	get_parent_scope()->dump(os);
}

void function_scope_t::dump(std::ostream &os) const {
	os << std::endl << "FUNCTION SCOPE: " << scope_name << std::endl;
	dump_bindings(os, bound_vars, {});
	dump_type_map(os, nominal_env, "FUNCTION NOMINAL ENV");
	dump_type_map(os, structural_env, "FUNCTION STRUCTURAL ENV");
	dump_type_map(os, type_variable_bindings, "FUNCTION TYPE VARIABLE BINDINGS");
	get_parent_scope()->dump(os);
}

void local_scope_t::dump(std::ostream &os) const {
	os << std::endl << "LOCAL SCOPE: " << scope_name << std::endl;
	dump_bindings(os, bound_vars, {});
	dump_type_map(os, nominal_env, "LOCAL NOMINAL ENV");
	dump_type_map(os, structural_env, "LOCAL STRUCTURAL ENV");
	dump_type_map(os, type_variable_bindings, "LOCAL TYPE VARIABLE BINDINGS");
	get_parent_scope()->dump(os);
}

generic_substitution_scope_t::generic_substitution_scope_t(
        std::string name,
        scope_t::ref parent_scope,
        types::type_t::ref callee_signature) :
    scope_impl_t(name, parent_scope), callee_signature(callee_signature)
{
}

void generic_substitution_scope_t::dump(std::ostream &os) const {
	os << std::endl << "GENERIC SUBSTITUTION SCOPE: " << scope_name << std::endl;
	os << "For Callee Signature: " << callee_signature->str() << std::endl;
	dump_bindings(os, bound_vars, {});
	dump_type_map(os, nominal_env, "GENERIC SUBSTITUTION NOMINAL ENV");
	dump_type_map(os, structural_env, "GENERIC SUBSTITUTION STRUCTURAL ENV");
	dump_type_map(os, type_variable_bindings, "GENERIC SUBSTITUTION TYPE VARIABLE BINDINGS");
	get_parent_scope()->dump(os);
}

module_scope_impl_t::module_scope_impl_t(
		std::string name,
	   	program_scope_t::ref parent_scope,
		llvm::Module *llvm_module) :
	scope_impl_t<module_scope_t>(name, parent_scope),
   	llvm_module(llvm_module)
{
}

bool module_scope_impl_t::symbol_exists_in_running_scope(std::string symbol, bound_var_t::ref &bound_var) {
	return false;
}

std::string module_scope_impl_t::make_fqn(std::string leaf_name) const {
	if (leaf_name.find(SCOPE_SEP) != std::string::npos) {
	   	log("found a . in %s", leaf_name.c_str());
	   	dbg();
   	}
	auto scope_name = get_leaf_name();
	assert(scope_name.size() != 0);
	return scope_name + SCOPE_SEP + leaf_name;
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

		user_info(status, *unchecked_type_iter->second->node,
				"see type " c_type("%s") " declaration",
				unchecked_type_iter->second->name.c_str());
	}
}

unchecked_type_t::ref module_scope_impl_t::get_unchecked_type(std::string symbol) {
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
			type_function(
				make_iid("__init_module_vars"),
			   	type_id(make_iid("true")),
				type_args({}),
			   	type_id(make_iid("void"))),
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

	builder.SetInsertPoint(llvm_function->getEntryBlock().getTerminator());
}

unchecked_var_t::refs &program_scope_t::get_unchecked_vars_ordered() {
	return unchecked_vars_ordered;
}

unchecked_var_t::ref put_unchecked_variable_impl(
		std::string symbol,
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
		std::string symbol,
	   	unchecked_var_t::ref unchecked_variable)
{
	return put_unchecked_variable_impl(symbol, unchecked_variable,
			unchecked_vars, unchecked_vars_ordered);
}

unchecked_var_t::ref module_scope_t::put_unchecked_variable(
		std::string symbol,
	   	unchecked_var_t::ref unchecked_variable)
{
	return get_program_scope()->put_unchecked_variable(
			make_fqn(symbol),
		   	unchecked_variable);
}

unchecked_var_t::ref program_scope_t::get_unchecked_variable(std::string symbol) {
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
	if (source == dest) {
		log("bound type mapping is self-referential on %s", source.str().c_str());
		assert(false);
	}

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
	std::string signature = type->get_signature().repr();
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
		std::string name,
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

module_scope_t::ref program_scope_t::lookup_module(std::string symbol) {
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
		std::string name,
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

program_scope_t::ref program_scope_t::create(std::string name, compiler_t &compiler, llvm::Module *llvm_module) {
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
		if (pair.first.find("_") != 0) {
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

void put_typename_impl(
		status_t &status,
		scope_t::ref parent_scope,
		const std::string &scope_name,
		types::type_t::map &typename_env,
		const std::string &type_name,
		types::type_t::ref expansion,
		bool is_structural)
{
#if 0
	// A good place for a breakpoint when debugging type issues
	dbg_when(type_name.str().find("map.map") != std::string::npos);
#endif

	auto iter_type = typename_env.find(type_name);
	if (iter_type == typename_env.end()) {
		debug_above(2, log(log_info, "registering typename " c_type("%s") " as %s in scope " c_id("%s"),
					type_name.c_str(), expansion->str().c_str(),
					scope_name.c_str()));
		typename_env[type_name] = expansion;
		if (parent_scope != nullptr) {
			/* register this type with our parent */
			if (is_structural) {
				parent_scope->put_structural_typename(status, scope_name + SCOPE_SEP + type_name, expansion);
			} else {
				parent_scope->put_nominal_typename(status, scope_name + SCOPE_SEP + type_name, expansion);
			}
		} else {
			/* we are at the outermost scope, we're done. */
		}
	} else {
		user_error(status, expansion->get_location(),
				"multiple supertypes are not yet implemented (" c_type("%s") " <: " c_type("%s") ")",
				type_name.c_str(), expansion->str().c_str());
		auto existing_expansion = iter_type->second;
		user_info(status,
				existing_expansion->get_location(),
				"prior type definition for " c_type("%s") " is %s",
				type_name.c_str(),
				existing_expansion->str().c_str());
		dbg();
	}
}

bool module_scope_impl_t::has_bound(const std::string &name, const types::type_t::ref &type, bound_var_t::ref *var) const {
	// NOTE: for now this only really works for module and global variables
	assert(type->ftv_count() == 0);
	auto overloads_iter = bound_vars.find(name);
	if (overloads_iter != bound_vars.end()) {
		auto &overloads = overloads_iter->second;
		types::signature signature = type->get_signature();
		auto existing_bound_var_iter = overloads.find(signature);
		if (existing_bound_var_iter != overloads.end()) {
			if (var != nullptr) {
				*var = existing_bound_var_iter->second;
			}
			return true;
		}
	}

	if (dynamic_cast<const program_scope_t*>(this) != nullptr) {
		/* we are already at program scope, and we didn't find it */
		return false;
	} else {
		/* we didn't find that name in our bound vars, let's check if it's registered at global scope */
		bool found_at_global_scope = get_program_scope()->has_bound(make_fqn(name), type, var);

		// REVIEW: this really shouldn't happen, since if we are asking if something is bound, it
		// should be right before we would be instantiating it, which would be in the context of its
		// owning module...right?
		assert(!found_at_global_scope);

		return found_at_global_scope;
	}
}

void put_bound_function(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		std::string function_name,
		identifier::ref extends_module,
		bound_var_t::ref bound_function,
		local_scope_t::ref *new_scope)
{
	if (function_name.size() != 0) {
		auto program_scope = scope->get_program_scope();
		/* inline function definitions are scoped to the virtual block in which
		 * they appear */
		if (auto local_scope = dyncast<local_scope_t>(scope)) {
			*new_scope = local_scope->new_local_scope(
					string_format("function-%s", function_name.c_str()));

			(*new_scope)->put_bound_variable(status, function_name, bound_function);
		} else {
			module_scope_t::ref module_scope = dyncast<module_scope_t>(scope);

			if (module_scope == nullptr) {
				if (auto subst_scope = dyncast<generic_substitution_scope_t>(scope)) {
					module_scope = dyncast<module_scope_t>(subst_scope->get_parent_scope());
				}
			}

			if (module_scope != nullptr) {
				if (extends_module != nullptr) {
					std::string module_name = extends_module->get_name();
					if (module_name == GLOBAL_SCOPE_NAME) {
						program_scope->put_bound_variable(status, function_name, bound_function);
					} else if (auto injection_module_scope = program_scope->lookup_module(module_name)) {
						/* we're injecting this function into some other scope */
						injection_module_scope->put_bound_variable(status, function_name, bound_function);
					} else {
						assert(false);
					}
				} else {
					/* before recursing directly or indirectly, let's just add
					 * this function to the module scope we're in */
					module_scope->put_bound_variable(status, function_name, bound_function);
				}
			}
		}
	} else {
		user_error(status, bound_function->get_location(), "visible function definitions need names");
	}
}
