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
#include "dump.h"
#include "type_instantiation.h"
#include <iostream>
#include "atom.h"
#include "encoding.h"

const char *GLOBAL_ID = "_";
const token_kind SCOPE_TK = tk_dot;
const char SCOPE_SEP_CHAR = '.';
const char *SCOPE_SEP = ".";

function_scope_t::ref create_function_scope(std::string module_name, scope_t::ref parent_scope);

void get_callables_from_bound_vars(
		scope_t::ref scope,
		std::string symbol,
		const bound_var_t::map &bound_vars,
		var_t::refs &fns)
{
	std::set<location_t> locations;
	for (auto fn:fns) {
		locations.insert(fn->get_location());
	}
	auto iter = bound_vars.find(symbol);
	if (iter != bound_vars.end()) {
		const auto &overloads = iter->second;
		for (auto &pair : overloads) {
			auto &var = pair.second;
			if (var->type->is_function(scope)) {
				if (in(var->get_location(), locations)) {
					/* we must already have an unchecked version of this function, so let's hold off
					 * on adding this bound version because we probably need to check the type
					 * constraints again... */
					continue;
				} else {
					fns.push_back(var);
				}
			}
		}
	}
}

bound_var_t::ref get_bound_variable_from_scope(
		llvm::IRBuilder<> &builder,
		location_t location,
		std::string scope_name,
		std::string symbol,
		const bound_var_t::map &bound_vars,
		scope_t::ref parent_scope,
		scope_t::ref stopping_scope)
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
			throw user_error(location,
				   	"a reference to an overloaded variable " c_id("%s") " was found. overloads at this immediate location are:\n%s",
					symbol.c_str(),
					::str(overloads).c_str());
			return nullptr;
		}
	} else if (parent_scope != nullptr && parent_scope != stopping_scope) {
		return parent_scope->get_bound_variable(builder, location, symbol, stopping_scope);
	}

	debug_above(6, log(log_info,
			   	"no bound variable found when looking for " c_id("%s") " in " c_id("%s"), 
				symbol.c_str(),
				scope_name.c_str()));
	return nullptr;
}


template <typename BASE>
struct scope_impl_t : public virtual BASE {
	typedef ptr<BASE> ref;
	typedef ptr<const BASE> cref;

	scope_impl_t(std::string name, ptr<scope_t> parent_scope) :
		scope_name(name),
		parent_scope(parent_scope) {}

	virtual ~scope_impl_t() {}
	scope_impl_t(ptr<scope_t> parent_scope) = delete;
	scope_impl_t(const scope_impl_t &scope) = delete;

	virtual ptr<scope_t> this_scope() = 0;
	virtual ptr<const scope_t> this_scope() const = 0;
	virtual std::string get_leaf_name() const {
		return scope_name;
	}

	ptr<module_scope_t> get_module_scope() {
		if (auto module_scope = dyncast<module_scope_t>(this_scope())) {
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

	ptr<const module_scope_t> get_module_scope() const {
		if (auto module_scope = dyncast<const module_scope_t>(this_scope())) {
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

	ptr<program_scope_t> get_program_scope() {
		assert(!dynamic_cast<program_scope_t* const>(this));
		return this->get_parent_scope()->get_program_scope();
	}

	ptr<const program_scope_t> get_program_scope() const {
		assert(!dynamic_cast<const program_scope_t* const>(this));
		return this->get_parent_scope()->get_program_scope();
	}

	void put_structural_typename(const std::string &type_name, types::type_t::ref expansion) {
		assert(env_map.find(type_name) == env_map.end());
		put_typename_impl(get_parent_scope(), scope_name, env_map, type_name, expansion, true /*is_structural*/);
	}

	void put_nominal_typename(const std::string &type_name, types::type_t::ref expansion) {
		assert(env_map.find(type_name) == env_map.end());
		put_typename_impl(get_parent_scope(), scope_name, env_map, type_name, expansion, false /*is_structural*/);
	}

	void put_type_variable_binding(const std::string &name, types::type_t::ref type) {
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

	types::type_t::ref get_type(const std::string &name, bool allow_structural_types) const {
		auto iter = env_map.find(name);
		if (iter != env_map.end()) {
			return ((!iter->second.first /*structural*/ || allow_structural_types)
					? iter->second.second
					: nullptr);
		} else {
			auto parent_env = get_parent_scope();
			return ((parent_env != nullptr)
					? parent_env->get_type(name, allow_structural_types)
					: nullptr);
		}
	}

	types::type_t::map get_type_variable_bindings() const {
		auto parent_scope = this->get_parent_scope();
		if (parent_scope != nullptr) {
			return merge(parent_scope->get_type_variable_bindings(), type_variable_bindings);
		} else {
			return type_variable_bindings;
		}
	}

	std::string str() {
		std::stringstream ss;
		scope_t::ref p = this_scope();
		do {
			p->dump(ss);
		} while ((p = p->get_parent_scope()) != nullptr);
		return ss.str();
	}

	std::string make_fqn(std::string leaf_name) const {
		return get_parent_scope()->make_fqn(leaf_name);
	}

	void put_bound_variable(
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
				program_scope->put_bound_variable(make_fqn(symbol), bound_variable);
			}
		}
	}

	bound_var_t::ref get_singleton(std::string name) {
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

	bound_var_t::ref get_bound_function(std::string name, std::string signature) {
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

	virtual bound_var_t::ref get_bound_variable(
			llvm::IRBuilder<> &builder,
			location_t location,
			std::string symbol,
			scope_t::ref stopping_scope)
	{
		return ::get_bound_variable_from_scope(builder, location, this->get_name(),
				symbol, bound_vars,
			   	this->get_parent_scope() != stopping_scope ? this->get_parent_scope() : nullptr,
				stopping_scope);
	}

	bound_type_t::ref get_bound_type(types::signature signature, bool use_mappings) {
		return get_bound_type_from_scope(signature, get_program_scope(), use_mappings);
	}

	bool has_bound(const std::string &name, const types::type_t::ref &type, bound_var_t::ref *var) const {
		return get_parent_scope()->has_bound(name, type, var);
	}

	void get_callables(
			std::string symbol,
			var_t::refs &fns,
			bool check_unchecked)
	{
		// TODO: clean up this horrible mess
		auto module_scope = dynamic_cast<module_scope_t*>(this);

		if (module_scope != nullptr) {
			/* default scope behavior is to look at bound variables */
			get_callables_from_bound_vars(this_scope(), symbol, bound_vars, fns);

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

	ptr<scope_t> get_parent_scope() {
		return parent_scope;
	}

	ptr<const scope_t> get_parent_scope() const {
		return parent_scope;
	}

	std::string get_name() const {
		auto parent_scope = this->get_parent_scope();
		if (parent_scope && !dyncast<const program_scope_t>(parent_scope)) {
			return parent_scope->get_name() + SCOPE_SEP + get_leaf_name();
		} else {
			return get_leaf_name();
		}
	}

	std::string scope_name;

	scope_t::ref parent_scope;
	bound_var_t::map bound_vars;
	env_map_t env_map;
	types::type_t::map type_variable_bindings;
};

struct closure_scope_impl_t final : public std::enable_shared_from_this<closure_scope_impl_t>, scope_impl_t<closure_scope_t> {
	virtual ~closure_scope_impl_t() {}
	closure_scope_impl_t(
			std::string name,
			module_scope_t::ref parent_scope,
			runnable_scope_t::ref runnable_scope,
			llvm::IRBuilder<> &capture_builder) :
		scope_impl_t<closure_scope_t>(name, parent_scope),
		running_scope(runnable_scope),
		capture_builder(capture_builder)
	{}

	ptr<scope_t> this_scope() override {
		return this->shared_from_this();
	}
	ptr<const scope_t> this_scope() const override {
		return this->shared_from_this();
	}

	void dump(std::ostream &os) const override {
		assert(false);
	}

	ptr<function_scope_t> new_function_scope(std::string name) override {
		debug_above(8, log("creating a function scope %s within scope %s", name.c_str(), this->get_name().c_str()));
		return create_function_scope(name, shared_from_this());
	}

	llvm::Module *get_llvm_module() override {
		return this->get_parent_scope()->get_llvm_module();
	}

	void set_capture_env(bound_var_t::ref capture_env) override {
		assert(this->capture_env == nullptr);
		this->capture_env = capture_env;
	}

	struct capture_t {
		/* each time we capture a value from the running scope, we create a capture_t */
		std::string name;
		bound_var_t::ref original_value;
		bound_var_t::ref value_in_closure;
	};

	static llvm::StructType *_create_temporary_capture_type(
			llvm::IRBuilder<> &builder,
			program_scope_t::ref program_scope,
			std::string scope_name,
			std::string symbol,
			bound_var_t::ref loaded_value,
			const std::vector<capture_t> &captures,
			std::vector<llvm::Type*> &llvm_types)
	{
		bound_type_t::ref bound_var_type = program_scope->get_runtime_type(builder, STD_MANAGED_TYPE, false /*get_ptr*/);

		llvm::StructType *llvm_var_type = llvm::dyn_cast<llvm::StructType>(bound_var_type->get_llvm_type());
		assert(llvm_var_type != nullptr);

		llvm_types.resize(0);
		llvm_types.reserve(2 + captures.size() + 1);

		/* the managed type */
		llvm_types.push_back(llvm_var_type);

		/* the eventual function pointer */
		llvm_types.push_back(llvm::FunctionType::get(builder.getVoidTy(), {}, false /*isVarArg*/)->getPointerTo());

		for (auto &capture : captures) {
			llvm::Type *llvm_type = capture.value_in_closure->type->get_llvm_specific_type();
			llvm_types.push_back(llvm_type);
		}
		/* NB: this is relying on a transitive property of structure packing. that if we add another element to the
		 * structure, all prior elements will stay in their prior location */
		llvm_types.push_back(loaded_value->type->get_llvm_specific_type());

		return llvm_create_struct_type(
				builder,
				scope_name + string_format(" (capture %d for %s)", (int)captures.size(), symbol.c_str()),
				llvm_types);
	}

	std::function<bool (const capture_t &)> capture_finder(std::string symbol) {
		return [symbol](const capture_t &capture) -> bool {
			return capture.name == symbol;
		};
	}

	static capture_t create_capture(
			llvm::IRBuilder<> &builder,
			program_scope_t::ref program_scope,
			std::string scope_name,
			location_t location,
			std::string symbol,
			bound_var_t::ref loaded_value,
			bound_var_t::ref capture_env,
			const std::vector<capture_t> &captures)
	{
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		builder.SetInsertPoint(llvm_get_function(builder)->getEntryBlock().getTerminator());

		std::vector<llvm::Type*> llvm_types;
		llvm::StructType *llvm_struct_type = _create_temporary_capture_type(builder, program_scope, scope_name, symbol, loaded_value, captures, llvm_types);

		/* now cast the capture_env to be a pointer to the new closure env type */
		llvm::Value *llvm_capture_env = builder.CreateBitCast(capture_env->get_llvm_value(), llvm_struct_type->getPointerTo());
		std::vector<llvm::Value *> gep_path = std::vector<llvm::Value *>{
			builder.getInt32(0),
			/* get the last item from the closure env (so far) */
			builder.getInt32(llvm_types.size() - 1),
		};

		debug_above(8, log("llvm_capture_env is %s", llvm_print(llvm_capture_env).c_str()));
		/* insert the captured loaded value at the end of the entry block */
		llvm::Value *llvm_closure_value_pointer = builder.CreateInBoundsGEP(llvm_capture_env, gep_path);

		bound_var_t::ref value_in_closure = bound_var_t::create(
				INTERNAL_LOC(),
				symbol,
				loaded_value->type,
				builder.CreateLoad(llvm_closure_value_pointer),
				make_iid_impl(symbol, location));


		/* sweet, we found a value we can capture */
		capture_t capture;
		capture.name = symbol;
		capture.original_value = loaded_value;
		capture.value_in_closure = value_in_closure;
		return capture;
	}

	bound_var_t::ref capture_hunt(
			llvm::IRBuilder<> &builder,
			location_t location,
			std::string symbol,
			scope_t::ref stopping_scope)
	{
		// TODO: look in running scope for this symbol, if it exists, capture it and return a load from the value in the
		// closure.
		module_scope_t::ref module_scope = dyncast<module_scope_t>(get_parent_scope());
		assert(module_scope != nullptr);

		bound_var_t::ref capturable_value = running_scope->get_bound_variable(
				capture_builder, location, symbol, module_scope/*stopping_scope*/);

		assert(none_of(captures.begin(), captures.end(), capture_finder(symbol)));
		if (capturable_value != nullptr) {
			assert(capture_builder.GetInsertBlock() != builder.GetInsertBlock());
			bound_var_t::ref loaded_value = capturable_value->resolve_bound_value(capture_builder, running_scope);
			captures.push_back(create_capture(builder, get_program_scope(), get_leaf_name(), location, symbol, loaded_value, capture_env, captures));
			return captures.back().value_in_closure;
		} else {
			/* we did not find anything to capture */
			return nullptr;
		}
	}

	// TODO: handle get_bound_function or whatev
	//
	bound_var_t::ref get_bound_variable(
			llvm::IRBuilder<> &builder,
			location_t location,
			std::string symbol,
			scope_t::ref stopping_scope) override
	{
		assert(stopping_scope != shared_from_this());
		debug_above(7, log("trying to find symbol %s in closure", symbol.c_str()));
		auto capture_iter = std::find_if(captures.begin(), captures.end(), capture_finder(symbol));
		if (capture_iter == captures.end()) {
			/* we haven't captured this, or it just doesn't make sense, keep looking */
			auto bound_var = capture_hunt(builder, location, symbol, stopping_scope);
			if (bound_var != nullptr) {
				return bound_var;
			} else {
				/* fallback to module scope */
				auto parent_scope = get_parent_scope();
				if (parent_scope != stopping_scope && parent_scope != nullptr) {
					return parent_scope->get_bound_variable(builder, location, symbol, stopping_scope);
				} else {
					return nullptr;
				}
			}
		} else {
			return capture_iter->value_in_closure;
		}
	}

	bound_var_t::ref create_closure(
			llvm::IRBuilder<> &builder,
			ptr<life_t> life,
			location_t location,
			bound_var_t::ref function) override
	{
		debug_above(6, log("create_closure(..., %s, %s)", location.str().c_str(), function->str().c_str()));
		types::type_t::refs dimensions;
		types::name_index_t name_index;
		identifier::refs dim_ids;

		dimensions.push_back(function->type->get_type());
		name_index.insert({"__fn", 0});
		dim_ids.push_back(make_iid_impl("__fn", function->get_location()));

		int i = 1;
		for (auto &capture : captures) {
			dimensions.push_back(capture.value_in_closure->type->get_type());
			name_index.insert({capture.name, i++});
			dim_ids.push_back(make_iid_impl(capture.name, capture.original_value->get_location()));
		}

		/* let's create a user type for this closure */
		types::type_function_t::ref data_ctor_type = type_function(
				nullptr, type_args(dimensions, dim_ids),
				type_ptr(type_managed(type_struct(dimensions, name_index))));

		/* create a constructor for this type */
		bound_var_t::ref ctor_fn = bind_ctor_to_scope(
				builder, get_parent_scope(),
				make_iid_impl("closure", location), location,
				data_ctor_type);

		bound_var_t::refs bound_dimensions;
		bound_dimensions.push_back(function);
		for (auto &capture : captures) {
			bound_dimensions.push_back(capture.original_value);
		}

		/* call the constructor */
		bound_var_t::ref constructed_closure = create_callsite(
				builder,
				shared_from_this(),
				life,
				ctor_fn,
				"closure ctor",
				location,
				bound_dimensions);

		types::type_function_t::ref type_function = dyncast<const types::type_function_t>(function->type->get_type());
		assert(type_function != nullptr);
		debug_above(8, log("type_function is %s", type_function->str().c_str()));

		types::type_args_t::ref type_args = dyncast<const types::type_args_t>(type_function->args);
		assert(type_args != nullptr);

		types::type_t::refs args = type_args->args;
		identifier::refs names = type_args->names;

		assert(args.size() >= 1);
		args.resize(args.size() - 1);
		if (names.size() >= 1) {
			names.resize(names.size() - 1);
		}

		types::type_function_t::ref type_function_unbound = ::type_function(
				type_function->type_constraints,
				::type_args(args, names),
				type_function->return_type);

		types::type_function_closure_t::ref closure_type = type_function_closure(type_function_unbound);
		bound_type_t::ref bound_closure_type = upsert_bound_type(builder, running_scope, closure_type);
		llvm::Value *llvm_function_closure_value = builder.CreateBitCast(
				constructed_closure->get_llvm_value(),
				bound_closure_type->get_llvm_specific_type());

		return bound_var_t::create(
				INTERNAL_LOC(),
				"opaque closure",
				bound_closure_type,
				llvm_function_closure_value,
				make_iid("opaque closure"));
	}

	/* the capture builder will emit loads so that they can be copied into the closure. note that it is doing this back
	 * in the context of the running scope where this closure is being instantiated, so we need this builder in order to
	 * remember the context of where we were at the time of capture. */
	runnable_scope_t::ref running_scope;
	llvm::IRBuilder<> &capture_builder;
	std::vector<capture_t> captures;
	bound_var_t::ref capture_env;
};

typedef bound_type_t::ref return_type_constraint_t;

template <typename T>
struct runnable_scope_impl_t : public std::enable_shared_from_this<runnable_scope_impl_t<T>>, scope_impl_t<T> {
	/* runnable scopes are those that can instantiate local scopes */
	typedef ptr<T> ref;

	virtual ~runnable_scope_impl_t() {}
	ptr<scope_t> this_scope() {
		return this->shared_from_this();
	}
	ptr<const scope_t> this_scope() const {
		return this->shared_from_this();
	}

	runnable_scope_impl_t(
			std::string name,
			scope_t::ref parent_scope,
			return_type_constraint_t &return_type_constraint) :
		scope_impl_t<T>(name, parent_scope),
		return_type_constraint(return_type_constraint)
	{
	}

	runnable_scope_impl_t() = delete;
	runnable_scope_impl_t(const T &) = delete;

	template <typename Q>
	static ptr<Q> create(
			std::string name,
			scope_t::ref parent_scope,
			return_type_constraint_t &return_type_constraint)
	{
		return make_ptr<runnable_scope_impl_t<Q>>(name, parent_scope, return_type_constraint);
	}

	ptr<runnable_scope_t> new_runnable_scope(std::string name) {
		return create<runnable_scope_t>(name, this->shared_from_this(), this->return_type_constraint);
	}

	ptr<closure_scope_t> new_closure_scope(llvm::IRBuilder<> &builder, std::string name) {
		return make_ptr<closure_scope_impl_t>(name, this->get_module_scope(), this->shared_from_this(), builder);
	}

	ptr<function_scope_t> new_function_scope(std::string name) {
		debug_above(8, log("creating a function scope %s within scope %s", name.c_str(), this->get_name().c_str()));
		return create_function_scope(name, this->get_module_scope());
	}

	llvm::Module *get_llvm_module() {
		return this->get_parent_scope()->get_llvm_module();
	}

	void dump(std::ostream &os) const {
		os << std::endl << "LOCAL SCOPE: " << this->scope_name << std::endl;
		os << "Return type: " << return_type_constraint->str() << std::endl;
		dump_bindings(os, this->bound_vars, {});
		dump_env_map(os, this->env_map, "LOCAL ENV");
		dump_type_map(os, this->type_variable_bindings, "LOCAL TYPE VARIABLE BINDINGS");
		this->get_parent_scope()->dump(os);
	}

	return_type_constraint_t &get_return_type_constraint() {
		return return_type_constraint;
	}

	friend struct loop_tracker_t;
	void set_innermost_loop_bbs(llvm::BasicBlock *new_loop_continue_bb, llvm::BasicBlock *new_loop_break_bb) {
		assert(new_loop_continue_bb != loop_continue_bb);
		assert(new_loop_break_bb != loop_break_bb);

		loop_continue_bb = new_loop_continue_bb;
		loop_break_bb = new_loop_break_bb;
	}

	void check_or_update_return_type_constraint(
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
					this->shared_from_this(),
					this->get_type_variable_bindings());

			if (!unification.result) {
				// TODO: consider directional unification here
				// TODO: consider storing more useful info in return_type_constraint
				throw user_error(return_statement->get_location(),
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

	llvm::BasicBlock *get_innermost_loop_break() const {
		/* regular scopes (not runnable scope) doesn't have the concept of loop
		 * exits */
		if (loop_break_bb == nullptr) {
			if (auto parent_scope = dyncast<const runnable_scope_t>(this->get_parent_scope())) {
				return parent_scope->get_innermost_loop_break();
			} else {
				return nullptr;
			}
		} else {
			return loop_break_bb;
		}
	}

	llvm::BasicBlock *get_innermost_loop_continue() const {
		/* regular scopes (not runnable scope) doesn't have the concept of loop
		 * exits */
		if (loop_continue_bb == nullptr) {
			if (auto parent_scope = dyncast<const runnable_scope_t>(this->get_parent_scope())) {
				return parent_scope->get_innermost_loop_continue();
			} else {
				return nullptr;
			}
		} else {
			return loop_continue_bb;
		}
	}

private:
	llvm::BasicBlock *loop_break_bb = nullptr;
	llvm::BasicBlock *loop_continue_bb = nullptr;
	return_type_constraint_t &return_type_constraint;
};

template <typename T>
struct module_scope_impl_t : public scope_impl_t<T> {
	typedef ptr<T> ref;
	typedef std::map<std::string, ref> map;

	module_scope_impl_t() = delete;
	module_scope_impl_t(
			std::string name,
			program_scope_t::ref parent_scope,
			llvm::Module *llvm_module) :
		scope_impl_t<T>(name, parent_scope),
		llvm_module(llvm_module)
	{
	}

	ptr<function_scope_t> new_function_scope(std::string name) {
		debug_above(8, log("creating a function scope %s within scope %s", name.c_str(), this->get_name().c_str()));
		return create_function_scope(name, this->this_scope());
	}

	virtual ~module_scope_impl_t() {}

	unchecked_var_t::ref put_unchecked_variable(std::string symbol, unchecked_var_t::ref unchecked_variable) {
		return this->scope_impl_t<T>::get_program_scope()->put_unchecked_variable(make_fqn(symbol), unchecked_variable);
	}

	void put_unchecked_type(unchecked_type_t::ref unchecked_type) {
		// assert(unchecked_type->name.str().find(SCOPE_SEP) != std::string::npos);
		debug_above(6, log(log_info, "registering an unchecked type " c_type("%s") " %s in scope " c_id("%s"),
					unchecked_type->name.c_str(),
					unchecked_type->str().c_str(),
					this->get_name().c_str()));

		auto unchecked_type_iter = unchecked_types.find(unchecked_type->name);

		if (unchecked_type_iter == unchecked_types.end()) {
			unchecked_types.insert({unchecked_type->name, unchecked_type});

			/* also keep an ordered list of the unchecked types */
			unchecked_types_ordered.push_back(unchecked_type);
		} else {
			/* this unchecked type already exists */
			auto error = user_error(unchecked_type->node->get_location(), "type " c_type("%s") " already exists",
					unchecked_type->name.c_str());
			error.add_info(unchecked_type_iter->second->node->get_location(),
					"see type " c_type("%s") " declaration",
					unchecked_type_iter->second->name.c_str());
			throw error;
		}
	}

	unchecked_type_t::ref get_unchecked_type(std::string symbol) {
		auto iter = unchecked_types.find(symbol);
		if (iter != unchecked_types.end()) {
			return iter->second;
		} else {
			return nullptr;
		}
	}

	llvm::Module *get_llvm_module() {
		return llvm_module;
	}

	unchecked_type_t::refs &get_unchecked_types_ordered() {
		return unchecked_types_ordered;
	}

	void dump_tags(std::ostream &os) const {
		// dump_unchecked_var_tags(os, unchecked_vars);
	}

	std::string make_fqn(std::string leaf_name) const {
		if (leaf_name.find(SCOPE_SEP) != std::string::npos) {
			log("found a . in %s", leaf_name.c_str());
			dbg();
		}
		auto scope_name = this->get_leaf_name();
		assert(scope_name.size() != 0);
		return scope_name + SCOPE_SEP + leaf_name;
	}

	void dump(std::ostream &os) const {
		os << std::endl << "MODULE SCOPE: " << this->get_name() << std::endl;
		dump_bindings(os, this->bound_vars, {});
		dump_unchecked_types(os, unchecked_types);
		dump_env_map(os, this->env_map, "MODULE ENV");
		dump_type_map(os, this->type_variable_bindings, "MODULE TYPE VARIABLE BINDINGS");
		this->get_parent_scope()->dump(os);
	}

	bool has_bound(const std::string &name, const types::type_t::ref &type, bound_var_t::ref *var) const {
		// NOTE: for now this only really works for module and global variables
		auto overloads_iter = this->scope_impl_t<T>::bound_vars.find(name);
		if (overloads_iter != this->scope_impl_t<T>::bound_vars.end()) {
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
			bool found_at_global_scope = this->scope_impl_t<T>::get_program_scope()->has_bound(make_fqn(name), type, var);

			// REVIEW: this really shouldn't happen, since if we are asking if something is bound, it
			// should be right before we would be instantiating it, which would be in the context of its
			// owning module...right?
			assert(!found_at_global_scope);

			return found_at_global_scope;
		}
	}

protected:
	llvm::Module * const llvm_module;

	/* modules can have unchecked types */
	unchecked_type_t::map unchecked_types;

	/* let code look at the ordered list for iteration purposes */
	unchecked_type_t::refs unchecked_types_ordered;
};

struct module_scope_impl_impl_t final : public std::enable_shared_from_this<module_scope_impl_impl_t>, public module_scope_impl_t<module_scope_t> {
	module_scope_impl_impl_t(
			std::string name,
			program_scope_t::ref parent_scope,
			llvm::Module *llvm_module) :
		module_scope_impl_t<module_scope_t>(name, parent_scope, llvm_module)
	{
	}
	virtual ~module_scope_impl_impl_t() {}

	ptr<scope_t> this_scope() {
		return this->shared_from_this();
	}
	ptr<const scope_t> this_scope() const {
		return this->shared_from_this();
	}
	unchecked_var_t::ref get_unchecked_variable(std::string symbol) {
		return get_program_scope()->get_unchecked_variable(make_fqn(symbol));
	}
};

std::string str(const module_scope_t::map &modules);


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
					dyncast<const ast::data_type_t>(var->node) ||
					dyncast<const ast::link_function_statement_t>(var->node));
			fns.push_back(var);
		}
	}
}

/* scope keeps tabs on the bindings of variables, noting their declared
 * type as it goes */
struct program_scope_impl_t final : public std::enable_shared_from_this<program_scope_impl_t>, public module_scope_impl_t<program_scope_t> {
	typedef ptr<program_scope_t> ref;

	program_scope_impl_t(
			std::string name,
			compiler_t &compiler,
			llvm::Module *llvm_module) :
		module_scope_impl_t<program_scope_t>(name, nullptr, llvm_module),
		compiler(compiler) {}

	program_scope_impl_t() = delete;
	virtual ~program_scope_impl_t() {}

	ptr<scope_t> this_scope() {
		return this->shared_from_this();
	}
	ptr<const scope_t> this_scope() const {
		return this->shared_from_this();
	}

	std::string make_fqn(std::string name) const {
		return name;
	}

	ptr<program_scope_t> get_program_scope() {
		return dyncast<program_scope_t>(this_scope());
	}

	ptr<const program_scope_t> get_program_scope() const {
		return dyncast<const program_scope_t>(this_scope());
	}

	void dump(std::ostream &os) const {
		os << std::endl << "PROGRAM SCOPE: " << scope_name << std::endl;
		dump_bindings(os, bound_vars, bound_types);
		dump_unchecked_vars(os, unchecked_vars);
		dump_unchecked_types(os, unchecked_types);
		dump_env_map(os, env_map, "PROGRAM ENV");
		dump_type_map(os, type_variable_bindings, "PROGRAM TYPE VARIABLE BINDINGS");
	}

	std::string dump_llvm_modules() {
		std::stringstream ss;
		for (auto &module_pair : modules) {
			ss << C_MODULE << "MODULE " << C_RESET << module_pair.first << std::endl;
			ss << llvm_print_module(*module_pair.second->get_llvm_module());
		}
		return ss.str();
	}

	void dump_tags(std::ostream &os) const {
		dump_unchecked_var_tags(os, unchecked_vars);
		dump_unchecked_type_tags(os, unchecked_types);
	}

	ptr<module_scope_t> new_module_scope(std::string name, llvm::Module *llvm_module) {
		assert(!lookup_module(name));

		auto module_scope = make_ptr<module_scope_impl_impl_t>(name, get_program_scope(), llvm_module);
		modules.insert({name, module_scope});
		return module_scope;
	}

	bound_var_t::ref upsert_init_module_vars_function(llvm::IRBuilder<> &builder) {
		if (init_module_vars_function != nullptr) {
			return init_module_vars_function;
		}

		/* build the global __init_module_vars function */
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);

		/* we are creating this function, but we'll be adding to it elsewhere */
		init_module_vars_function = llvm_start_function(
				builder, 
				this_scope(),
				INTERNAL_LOC(),
				type_function(
					nullptr,
					type_args({}),
					type_id(make_iid("void"))),
				"__init_module_vars");

		builder.CreateRetVoid();

		put_bound_variable("__init_module_vars", init_module_vars_function);

		return init_module_vars_function;
	}

	void set_insert_point_to_init_module_vars_function(
			llvm::IRBuilder<> &builder,
			std::string for_var_decl_name)
	{
		auto fn = upsert_init_module_vars_function(builder);
		llvm::Function *llvm_function = llvm::dyn_cast<llvm::Function>(fn->get_llvm_value());
		assert(llvm_function != nullptr);

		builder.SetInsertPoint(llvm_function->getEntryBlock().getTerminator());
	}

	void get_callables(
			std::string symbol,
			var_t::refs &fns,
			bool check_unchecked)
	{
		if (check_unchecked) {
			get_callables_from_unchecked_vars(symbol, unchecked_vars, fns);
		}
		get_callables_from_bound_vars(this_scope(), symbol, bound_vars, fns);
	}

	llvm::Type *get_llvm_type(location_t location, std::string type_name) {
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

		throw user_error(location, "couldn't find type " c_type("%s"), type_name.c_str());
		return nullptr;
	}

	llvm::Function *get_llvm_function(location_t location, std::string function_name) {
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

		throw user_error(location, "couldn't find function " c_var("%s"), function_name.c_str());
		return nullptr;
	}

	/* this is meant to be called when we know we're looking in program scope.
	 * this is not an implementation of get_symbol.  */
	module_scope_t::ref lookup_module(std::string symbol) {
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

	unchecked_var_t::ref get_unchecked_variable(std::string symbol) {
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

	unchecked_var_t::ref put_unchecked_variable(
			std::string symbol,
			unchecked_var_t::ref unchecked_variable)
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

	bound_type_t::ref get_bound_type(types::signature signature, bool use_mappings) {
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
				return get_bound_type(dest_iter->second, true /*use_mappings*/);
			}
		}

		debug_above(9, log(log_info, "nope. %s is not yet bound",
					signature.str().c_str()));
		return nullptr;
	}

	void put_bound_type(bound_type_t::ref type) {
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
			throw user_error(type->get_location(), "type %s already exists",
					type->str().c_str());
			throw user_error(iter->second->get_location(), "type %s was declared here",
					iter->second->str().c_str());
		}
	}

	void put_bound_type_mapping(
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
			throw user_error(INTERNAL_LOC(), "bound type mapping %s already exists!",
					source.str().c_str());
		}
	}

	unchecked_var_t::map unchecked_vars;

	unchecked_var_t::refs &get_unchecked_vars_ordered() {
		return unchecked_vars_ordered;
	}

	bound_type_t::ref get_runtime_type(
			llvm::IRBuilder<> &builder,
			std::string name,
			bool get_ptr)
	{
		auto type = type_id(make_iid_impl(name, INTERNAL_LOC()));
		if (get_ptr) {
			type = type_ptr(type);
		}
		return upsert_bound_type(builder, this_scope(), type);
	}

	bound_var_t::ref make_matcher(
			llvm::IRBuilder<> &builder,
			scope_t::ref scope,
		   	location_t location,
		   	types::type_t::ref type)
	{
		auto type_name = type->repr();
		debug_above(8, log("looking for a type matcher for %s", type->str().c_str()));
		bound_var_t::ref &matcher = bound_type_matchers[type_name];
		if (matcher == nullptr) {
			debug_above(8, log("size of bound_type_matchers is %d", int(bound_type_matchers.size())));
			debug_above(8, log("making type matcher for %s", type->str().c_str()));
			auto function_name = std::string("__type_matcher.") + type_name;
			types::type_t::refs args;
			/* the pattern to match */
			args.push_back(type_ptr(::type_integer(
							type_literal({INTERNAL_LOC(), tk_integer, ZION_TYPEID_BITSIZE_STR}),
							type_id(make_iid("false" /*signed*/)))));

			/* the starting position for matching */
			args.push_back(::type_integer(
						type_literal({INTERNAL_LOC(), tk_integer, "32"}),
						type_id(make_iid("false" /*signed*/))));

			/* return the next position - 0 on no progress */
			types::type_t::ref return_type = args.back();

			types::type_args_t::ref type_args = ::type_args(args, {});
			types::type_function_t::ref type_function = ::type_function(nullptr, type_args, return_type);

			auto bound_function_type = upsert_bound_type(builder, scope, type_function);
			debug_above(8, log("bound function type for matcher is %s", bound_function_type->str().c_str()));

			/* save and later restore the current branch insertion point */
			llvm::IRBuilderBase::InsertPointGuard ipg(builder);

			llvm::FunctionType *llvm_function_type = llvm::dyn_cast<llvm::FunctionType>(bound_function_type->get_llvm_specific_type()->getPointerElementType());
			assert(llvm_function_type != nullptr);
			debug_above(8, log("llvm type for matcher is %s", llvm_print(llvm_function_type).c_str()));

			/* now let's generate our actual data ctor fn */
			auto llvm_function = llvm::Function::Create(
					llvm_function_type,
					llvm::Function::ExternalLinkage, 
					function_name,
					get_llvm_module());

			llvm_function->setDoesNotThrow();
			llvm_function->addFnAttr(llvm::Attribute::AlwaysInline);

			/* create the actual bound variable for the fn */
			bound_var_t::ref function = bound_var_t::create(
					INTERNAL_LOC(), function_name,
					bound_function_type,
					llvm_function,
					make_iid_impl(function_name, location));

			/* store this matcher to break cycles of recursion in code generation (and to allow recursion in matching) */
			matcher = function;
			type = type->eval(scope);
			if (type->repr() != type_name) {
				assert(false);
				bound_type_matchers[type->repr()] = matcher;
			}

			builder.SetInsertPoint(llvm::BasicBlock::Create(builder.getContext(), "entry", llvm_function));
			llvm::BasicBlock *no_match_branch = llvm::BasicBlock::Create(builder.getContext(), "no.match", llvm_function);

			{
				/* implement the basic failure block - return null */
				llvm::IRBuilderBase::InsertPointGuard ipg(builder);
				builder.SetInsertPoint(no_match_branch);
				builder.CreateRet(builder.getInt32(0));
			}

			auto args_iter = llvm_function->arg_begin();
			llvm::Value *pattern = &*args_iter++;
			llvm::Value *pos = &*args_iter++;

			debug_above(8, log("making matcher for %s", type->str().c_str()));
			if (auto type_id = dyncast<const types::type_id_t>(type)) {
				make_match_id(builder, scope, location, type_name, type_id->repr(), llvm_function, no_match_branch, pattern, pos);
			} else if (auto type_integer = dyncast<const types::type_integer_t>(type)) {
				make_match_id(builder, scope, location, type_name, type_integer->repr(), llvm_function, no_match_branch, pattern, pos);
			} else if (auto type_operator = dyncast<const types::type_operator_t>(type)) {
				make_match_operator(builder, scope, location, type_name, type_operator, llvm_function, no_match_branch, pattern, pos);
			} else if (auto type_variable = dyncast<const types::type_variable_t>(type)) {
				log("matcher for type %s never matches", type->str().c_str());
				builder.CreateBr(no_match_branch);
			} else {
				log_location(log_error, location, "unable to make a matcher for %s in scope %s", type->str().c_str(), scope->get_name().c_str());
				log_location(log_error, location, "unable to make a matcher for %s", type->rebind(scope->get_type_variable_bindings())->eval(scope)->str().c_str());
				log("type variable bindings are %s", ::str(scope->get_parent_scope()->get_type_variable_bindings()).c_str());
				// unbound variables cannot be matched...
				dbg();
				assert(false);
			}

			debug_above(8, log("function is %s", llvm_print_function(llvm_function).c_str()));
		} else {
			debug_above(8, log("found type matcher for %s = %s", type->str().c_str(), matcher->str().c_str()));
		}

		return matcher;
	}

	void make_match_id(
			llvm::IRBuilder<> &builder,
			scope_t::ref scope,
		   	location_t location,
			std::string type_name,
			std::string id,
			llvm::Function *llvm_function,
			llvm::BasicBlock *no_match_branch,
			llvm::Value *pattern,
			llvm::Value *pos)
	{
		llvm::Value *pattern_word = get_word_at_offset(builder, pattern, pos, 0);
		llvm::Value *matched = builder.CreateICmpEQ(pattern_word, builder.getInt16(atomize(id)));
		auto success_block = llvm::BasicBlock::Create(
				builder.getContext(), "match", llvm_function);
		builder.CreateCondBr(matched, success_block, no_match_branch);

		builder.SetInsertPoint(success_block);

		builder.CreateRet(builder.CreateAdd(pos, builder.getInt32(1)));
	}

	void make_match_operator(
			llvm::IRBuilder<> &builder,
			scope_t::ref scope,
			location_t location,
			std::string type_name,
			types::type_operator_t::ref type_operator,
			llvm::Function *llvm_function,
			llvm::BasicBlock *no_match_branch,
			llvm::Value *pattern,
			llvm::Value *pos)
	{
		llvm::Value *pattern_word = get_word_at_offset(builder, pattern, pos, 0);

		llvm::Value *matched = builder.CreateICmpEQ(pattern_word, builder.getInt16(APPLY_INST));
		matched->setName("is_apply_inst");

		auto success_block = llvm::BasicBlock::Create(builder.getContext(), "is.apply", llvm_function);
		builder.CreateCondBr(matched, success_block, no_match_branch);

		builder.SetInsertPoint(success_block);

		auto lhs_matcher = make_matcher(builder, scope, location, type_operator->oper);

		std::vector<llvm::Value *> llvm_values{pattern, builder.CreateAdd(pos, builder.getInt32(1))};
		llvm::Value *next_pos = llvm_create_call_inst(
				builder,
				location,
				lhs_matcher,
				llvm_values);
		next_pos->setName(string_format("next_pos.after.%s", type_operator->oper->repr().c_str()));

		auto next_pos_is_zero = builder.CreateICmpEQ(next_pos, builder.getInt32(0));
		next_pos_is_zero->setName("no.match.lhs");

		auto rhs_check = llvm::BasicBlock::Create(builder.getContext(), "rhs.check", llvm_function);
		auto rhs_matcher = make_matcher(builder, scope, location, type_operator->operand);

		/* BLOCK */ {
			llvm::IRBuilderBase::InsertPointGuard ipg(builder);
			builder.SetInsertPoint(rhs_check);

			next_pos = llvm_create_call_inst(
					builder,
					location,
					rhs_matcher,
					std::vector<llvm::Value *>{pattern, next_pos});
			next_pos->setName(string_format("next_pos.after.%s", type_operator->operand->repr().c_str()));

			auto next_pos_is_zero_this_time = builder.CreateICmpEQ(next_pos, builder.getInt32(0));
			next_pos_is_zero_this_time->setName("no.match.rhs");

			auto match_branch = llvm::BasicBlock::Create(builder.getContext(), "match", llvm_function);

			/* BLOCK */ {
				llvm::IRBuilderBase::InsertPointGuard ipg(builder);
				builder.SetInsertPoint(match_branch);
				builder.CreateRet(next_pos);
			}
			builder.CreateCondBr(next_pos_is_zero_this_time, no_match_branch, match_branch);
		}

		builder.CreateCondBr(next_pos_is_zero, no_match_branch, rhs_check);
	}

	llvm::Value *get_word_at_offset(
			llvm::IRBuilder<> &builder,
			llvm::Value *array,
		   	llvm::Value *offset,
		   	int add)
   	{
		std::vector<llvm::Value *> gep_path;
		if (add != 0) {
			gep_path.push_back(builder.CreateAdd(offset, builder.getInt32(add)));
		} else {
			gep_path.push_back(offset);
		}

		/* insert the captured loaded value at the end of the entry block */
		return builder.CreateLoad(builder.CreateGEP(array, gep_path));
	}

private:
	compiler_t &compiler;
	module_scope_t::map modules;
	bound_type_t::map bound_types;
	std::map<types::signature, types::signature> bound_type_mappings;
	std::map<std::string, bound_var_t::ref> bound_type_matchers;

	/* track the module var initialization function */
	bound_var_t::ref init_module_vars_function;
	unchecked_var_t::refs initialized_module_vars;

	/* let code look at the ordered list for iteration purposes */
	unchecked_var_t::refs unchecked_vars_ordered;
};

struct function_scope_impl_t final : public runnable_scope_impl_t<function_scope_t> {
	typedef ptr<function_scope_t> ref;

	virtual ~function_scope_impl_t() {}
	function_scope_impl_t(std::string name, scope_t::ref parent_scope) :
		runnable_scope_impl_t(name, parent_scope, this->return_type_constraint) {}

	void dump(std::ostream &os) const {
		os << std::endl << "FUNCTION SCOPE: " << scope_name << std::endl;
		dump_bindings(os, bound_vars, {});
		dump_env_map(os, env_map, "FUNCTION ENV");
		dump_type_map(os, type_variable_bindings, "FUNCTION TYPE VARIABLE BINDINGS");
		get_parent_scope()->dump(os);
	}

	void set_return_type_constraint(return_type_constraint_t return_type_constraint) {
		assert(this->return_type_constraint == nullptr);
		this->return_type_constraint = return_type_constraint;
	}

	/* functions have return type constraints for use during type checking */
	return_type_constraint_t return_type_constraint;

};

function_scope_t::ref create_function_scope(std::string module_name, scope_t::ref parent_scope) {
	return make_ptr<function_scope_impl_t>(module_name, parent_scope);
}

struct generic_substitution_scope_impl_t final : public std::enable_shared_from_this<generic_substitution_scope_impl_t>, public scope_impl_t<generic_substitution_scope_t> {
	typedef ptr<generic_substitution_scope_t> ref;

	generic_substitution_scope_impl_t(
			std::string name,
			scope_t::ref parent_scope,
			types::type_t::ref callee_signature) :
		scope_impl_t(name, parent_scope), callee_signature(callee_signature)
	{
	}
	virtual ~generic_substitution_scope_impl_t() {}

	ptr<scope_t> this_scope() {
		return shared_from_this();
	}
	ptr<const scope_t> this_scope() const {
		return shared_from_this();
	}

	ptr<function_scope_t> new_function_scope(std::string name) {
		debug_above(8, log("creating a function scope %s within scope %s", name.c_str(), this->get_name().c_str()));
		assert(!dyncast<runnable_scope_t>(get_parent_scope()));
		return create_function_scope(name, this_scope());
	}

	void dump(std::ostream &os) const {
		os << std::endl << "GENERIC SUBSTITUTION SCOPE: " << scope_name << std::endl;
		os << "For Callee Signature: " << callee_signature->str() << std::endl;
		dump_bindings(os, bound_vars, {});
		dump_env_map(os, env_map, "GENERIC SUBSTITUTION ENV");
		dump_type_map(os, type_variable_bindings, "GENERIC SUBSTITUTION TYPE VARIABLE BINDINGS");
		get_parent_scope()->dump(os);
	}

	llvm::Module *get_llvm_module() {
		return get_parent_scope()->get_llvm_module();
	}

	static ref create(
			llvm::IRBuilder<> &builder,
			const ptr<const ast::item_t> &fn_decl,
			scope_t::ref module_scope,
			unification_t unification,
			types::type_t::ref callee_type);

	const types::type_t::ref callee_signature;
};

void put_typename_impl(
		scope_t::ref parent_scope,
		const std::string &scope_name,
		env_map_t &env_map,
		const std::string &type_name,
		types::type_t::ref expansion,
		bool is_structural);

bound_type_t::ref get_bound_type_from_scope(
		types::signature signature,
		program_scope_t::ref program_scope, bool use_mappings);

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

llvm::Module *scope_t::get_llvm_module() {
	if (get_parent_scope()) {
		return get_parent_scope()->get_llvm_module();
	} else {
		assert(false);
		return nullptr;
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

program_scope_t::ref program_scope_t::create(std::string name, compiler_t &compiler, llvm::Module *llvm_module) {
	return make_ptr<program_scope_impl_t>(name, compiler, llvm_module);
}

generic_substitution_scope_t::ref generic_substitution_scope_t::create(
		llvm::IRBuilder<> &builder,
		const ptr<const ast::item_t> &fn_decl,
		scope_t::ref parent_scope,
		unification_t unification,
		types::type_t::ref callee_type)
{
	/* instantiate a new scope */
	auto subst_scope = make_ptr<generic_substitution_scope_impl_t>(
			"generic substitution", parent_scope, callee_type);

	/* iterate over the bindings found during unifications and make
	 * substitutions in the type environment */
	for (auto &pair : unification.bindings) {
		if (pair.first.find("_") != 0) {
			subst_scope->put_type_variable_binding(pair.first, pair.second);
		} else {
			debug_above(7, log(log_info, "skipping adding %s to generic substitution scope",
						pair.first.c_str()));
		}
	}

	return subst_scope;
}

void put_typename_impl(
		scope_t::ref parent_scope,
		const std::string &scope_name,
		env_map_t &env_map,
		const std::string &type_name,
		types::type_t::ref expansion,
		bool is_structural)
{
#if 0
	// A good place for a breakpoint when debugging type issues
	dbg_when(type_name.str().find("map.map") != std::string::npos);
#endif

	auto iter_type = env_map.find(type_name);
	if (iter_type == env_map.end()) {
		debug_above(2, log(log_info, "registering " c_type("%s") " typename " c_type("%s") " as %s in scope " c_id("%s"),
					is_structural ? "structural" : "nominal",
					type_name.c_str(), expansion->str().c_str(),
					scope_name.c_str()));
		env_map[type_name] = {is_structural, expansion};
		if (parent_scope != nullptr) {
			/* register this type with our parent */
			if (is_structural) {
				parent_scope->put_structural_typename(scope_name + SCOPE_SEP + type_name, expansion);
			} else {
				parent_scope->put_nominal_typename(scope_name + SCOPE_SEP + type_name, expansion);
			}
		} else {
			/* we are at the outermost scope, we're done. */
		}
	} else {
		auto error = user_error(expansion->get_location(),
				"multiple supertypes are not yet implemented (" c_type("%s") " <: " c_type("%s") ")",
				type_name.c_str(), expansion->str().c_str());
		auto existing_expansion = iter_type->second;
		error.add_info(existing_expansion.second->get_location(),
				"prior type definition for " c_type("%s") " is %s",
				type_name.c_str(),
				existing_expansion.second->str().c_str());
		dbg();
		throw error;
	}
}

void put_bound_function(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		location_t location,
		std::string function_name,
		identifier::ref extends_module,
		bound_var_t::ref bound_function,
		runnable_scope_t::ref *new_scope)
{
	debug_above(2, log("putting bound function %s at %s", function_name.c_str(),
			bound_function->get_location().str().c_str()));
	if (function_name.size() != 0) {
		auto program_scope = scope->get_program_scope();
		/* inline function definitions are scoped to the virtual block in which
		 * they appear */
		if (auto local_scope = dyncast<runnable_scope_t>(scope)) {
			assert(new_scope != nullptr);
			*new_scope = local_scope->new_runnable_scope(
					string_format("function-%s", function_name.c_str()));

			(*new_scope)->put_bound_variable(function_name, bound_function);
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
						program_scope->put_bound_variable(function_name, bound_function);
					} else if (auto injection_module_scope = program_scope->lookup_module(module_name)) {
						/* we're injecting this function into some other scope */
						injection_module_scope->put_bound_variable(function_name, bound_function);
					} else {
						assert(false);
					}
				} else {
					/* before recursing directly or indirectly, let's just add
					 * this function to the module scope we're in */
					module_scope->put_bound_variable(function_name, bound_function);
				}
			}
		}
	} else {
		throw user_error(bound_function->get_location(), "visible function definitions need names");
	}
}
