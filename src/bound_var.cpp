#include "logger.h"
#include "bound_var.h"
#include "llvm_utils.h"
#include "llvm_types.h"
#include "ast.h"
#include "parser.h"
#include "scopes.h"

struct local_bound_var_t : public std::enable_shared_from_this<local_bound_var_t>, public bound_var_t {
	typedef ptr<const local_bound_var_t> ref;

	local_bound_var_t() = delete;
	local_bound_var_t(
			location_t internal_location,
			std::string name,
			bound_type_t::ref type,
			llvm::Value *llvm_value,
			identifier::ref id) :
		internal_location(internal_location),
		name(name),
		type(type),
		id(id),
		llvm_value(llvm_value)
	{
		assert(llvm_value != nullptr);
		assert(id != nullptr);
		assert(type != nullptr);
	}

	virtual ~local_bound_var_t() throw() {}

private:
	location_t internal_location;
	std::string const name;
	bound_type_t::ref const type;
	identifier::ref const id;

	llvm::Value * const llvm_value;

public:
	ptr<bound_var_t> this_bound_var() override {
		return this->shared_from_this();
	}

	ptr<const bound_var_t> this_bound_var() const override {
		return this->shared_from_this();
	}

	llvm::Value *get_llvm_value(ptr<scope_t> scope) const override {
		return llvm_value;
	}

	std::string str() const override {
		std::stringstream ss;
		if (debug_level() >= 1) {
			ss << C_VAR << name << C_RESET << " : ";
		}
		ss << *type;

		assert(llvm_value != nullptr);

		if (debug_level() >= 10) {
			ss << " IR: ";
			std::string llir = llvm_print(*llvm_value);
			trim(llir);
			ss << C_IR << llvm_value;
			ss << " : " << llir << C_RESET;
			ss << " " << internal_location.str();
		}
		return ss.str();
	}


	std::string get_signature() const override {
		return type->get_signature();
	}

	bound_type_t::ref get_bound_type() const override {
		return type;
	}
	
	types::type_t::ref get_type() const override {
		return type->get_type();
	}

	types::type_t::ref get_type(ptr<scope_t> scope) const override {
		return type->get_type();
	}

	identifier::ref get_id() const override {
		return id;
	}

	location_t get_location() const override {
		return id->get_location();
	}

	std::string get_name() const override {
		return name;
	}

	llvm::Value *resolve_bound_var_value(scope_t::ref scope, llvm::IRBuilder<> &builder) const override {
		if (type->get_type()->eval_predicate(tb_ref, scope)) {
			return builder.CreateLoad(llvm_value);
		}

		return llvm_value;
	}

	bound_var_t::ref resolve_bound_value(
			llvm::IRBuilder<> &builder,
			scope_t::ref scope) const override
	{
		if (auto ref_type = dyncast<const types::type_ref_t>(type->get_type())) {
			auto bound_type = upsert_bound_type(builder, scope, ref_type->element_type);
			return make_bound_var(
					INTERNAL_LOC(),
					this->name,
					bound_type,
					resolve_bound_var_value(scope, builder),
					this->id);
		}
		return shared_from_this();
	}

};

bound_var_t::ref make_bound_var(
		location_t internal_location,
		std::string name,
		bound_type_t::ref type,
		llvm::Value *llvm_value,
		identifier::ref id)
{
	assert(type != nullptr);
#ifdef ZION_DEBUG
	if (type->get_type()->eval_predicate(tb_ref, nullptr)) {
		assert(llvm::dyn_cast<llvm::AllocaInst>(llvm_value) || llvm_value->getType()->isPointerTy());
	}
#endif

	return make_ptr<local_bound_var_t>(internal_location, name, type, llvm_value, id);
}

std::ostream &operator <<(std::ostream &os, const bound_var_t &var) {
	return os << var.str();
}

std::string str(const bound_var_t::overloads &overloads) {
	std::stringstream ss;
	const char *indent = "\t";
	for (auto &var_overload : overloads) {
		ss << indent << var_overload.first << ": ";
	   	ss << var_overload.second->str() << std::endl;
	}
	return ss.str();
}

std::string str(const bound_var_t::refs &args) {
	std::stringstream ss;
	ss << "[";
	ss << join_str(args, ", ");
	ss << "]";
	return ss.str();
}

bound_type_t::refs get_bound_types(bound_var_t::refs values) {
	bound_type_t::refs types;
	types.reserve(values.size());

	for (auto value : values) {
		types.push_back(value->get_bound_type());
	}

	return types;
}

struct lazily_bound_var_impl_t : public std::enable_shared_from_this<lazily_bound_var_impl_t>, public bound_var_t {
	typedef ptr<lazily_bound_var_impl_t> ref;
	~lazily_bound_var_impl_t() throw() override {}
	lazily_bound_var_impl_t() = delete;
	lazily_bound_var_impl_t(ptr<closure_scope_t> closure_scope, bound_var_t::ref var, std::function<bound_var_t::ref (ptr<scope_t>)> resolver) :
		closure_scope(closure_scope),
		original_var(var),
		resolver(resolver)
	{
	}

private:
	ptr<closure_scope_t> closure_scope;
	bound_var_t::ref original_var;
	mutable bound_var_t::ref resolved_var;
	std::function<bound_var_t::ref (ptr<scope_t>)> resolver;

	ptr<bound_var_t> this_bound_var() override {
		return this->shared_from_this();
	}

	ptr<const bound_var_t> this_bound_var() const override {
		return this->shared_from_this();
	}

	void resolve_bindings(ptr<scope_t> usage_scope) const {
		if (resolved_var == nullptr) {
			INDENT(4, string_format("resolving bindings for %s for %s",
						original_var->str().c_str(),
						closure_scope->get_name().c_str()));
			resolved_var = resolver(usage_scope);
		}
		assert(resolved_var != nullptr);
	}

	llvm::Value *get_llvm_value(ptr<scope_t> scope) const override {
		resolve_bindings(scope);
		return resolved_var->get_llvm_value(scope);
	}

	std::string str() const override {
		std::stringstream ss;
		if (debug_level() >= 1) {
			ss << C_VAR << original_var->get_name() << C_RESET << " : ";
			if (resolved_var != nullptr) {
				ss << "[resolved] ";
			} else {
				ss << "[unresolved] ";
			}
		}
		ss << original_var->get_type();
		return ss.str();
	}


	std::string get_signature() const override {
		return original_var->get_signature();
	}

	bound_type_t::ref get_bound_type() const override {
		return original_var->get_bound_type();
	}
	
	types::type_t::ref get_type() const override {
		return original_var->get_type();
	}

	types::type_t::ref get_type(ptr<scope_t> scope) const override {
		return static_cast<const var_t*>(original_var.operator->())->get_type(scope);
	}

	identifier::ref get_id() const override {
		return original_var->get_id();
	}

	location_t get_location() const override {
		return original_var->get_location();
	}

	std::string get_name() const override {
		return original_var->get_name();
	}

	llvm::Value *resolve_bound_var_value(scope_t::ref scope, llvm::IRBuilder<> &builder) const override {
		resolve_bindings(scope);
		return resolved_var->resolve_bound_var_value(scope, builder);
	}

	bound_var_t::ref resolve_bound_value(
			llvm::IRBuilder<> &builder,
			scope_t::ref scope) const override
	{
		resolve_bindings(scope);
		return resolved_var->resolve_bound_value(builder, scope);
	}
};

struct bound_module_impl_t : public std::enable_shared_from_this<bound_module_impl_t>, public bound_module_t {
	typedef ptr<bound_module_impl_t> ref;

	location_t internal_location;
	std::string name;
	identifier::ref id;
	ptr<module_scope_t> module_scope;

	bound_module_impl_t(
			location_t internal_location,
			std::string name,
			identifier::ref id,
			module_scope_t::ref module_scope) :
		internal_location(internal_location),
		name(name),
		id(id),
		module_scope(module_scope)
	{
		assert(module_scope != nullptr);
	}

	~bound_module_impl_t() {
	}

	ptr<module_scope_t> get_module_scope() const override {
		return module_scope;
	}

	ptr<bound_var_t> this_bound_var() override {
		return this->shared_from_this();
	}

	ptr<const bound_var_t> this_bound_var() const override {
		return this->shared_from_this();
	}

	llvm::Value *get_llvm_value(ptr<scope_t> scope) const override {
		assert(false);
		return nullptr;
	}

	std::string str() const override {
		return get_type()->str();
	}

	types::type_t::ref get_type() const override {
		return module_scope->get_bound_type({"module"})->get_type();
	}

	types::type_t::ref get_type(ptr<scope_t> scope) const override {
		return module_scope->get_bound_type({"module"})->get_type();
	}

	bound_type_t::ref get_bound_type() const override {
		return module_scope->get_bound_type({"module"});
	}

	std::string get_signature() const override {
		return module_scope->get_bound_type({"module"})->get_signature();
	}

	location_t get_location() const override {
		return id->get_location();
	}

	identifier::ref get_id() const override {
		return id;
	}

    std::string get_name() const override {
		return name;
	}

	llvm::Value *resolve_bound_var_value(ptr<scope_t> scope, llvm::IRBuilder<> &builder) const override {
		assert(false);
		return nullptr;
	}

	bound_var_t::ref resolve_bound_value(llvm::IRBuilder<> &builder, ptr<scope_t> scope) const override {
		return shared_from_this();
	}
};

bound_var_t::ref make_lazily_bound_var(closure_scope_t::ref closure_scope, bound_var_t::ref var, std::function<bound_var_t::ref (ptr<scope_t>)> resolver) {
	debug_above(2, log("making lazily_bound_var(%s, %s)", closure_scope->get_name().c_str(), var->str().c_str()));

	return make_ptr<lazily_bound_var_impl_t>(closure_scope, var, resolver);
}

bound_var_t::ref make_bound_module(
			location_t internal_location,
			std::string name,
			identifier::ref id,
			ptr<module_scope_t> module_scope)
{
	debug_above(2, log("making bound_module(%s, %s, %s, ...)",
			internal_location.str().c_str(),
			name.c_str(),
			id->str().c_str()));

	return make_ptr<bound_module_impl_t>(internal_location, name, id, module_scope);
}

