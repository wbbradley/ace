#include "zion.h"
#include "dbg.h"
#include "types.h"
#include <sstream>
#include "utils.h"
#include "types.h"
#include "parser.h"
#include <iostream>

const char *BUILTIN_NIL_TYPE = "nil";
const char *STD_LIST_TYPE = "std/list";
const char *BUILTIN_VOID_TYPE = "void";
const char *BUILTIN_UNREACHABLE_TYPE = "__unreachable";

int next_generic = 1;

void reset_generics() {
	next_generic = 1;
}

atom get_name_from_index(const types::name_index_t &name_index, int i) {
	atom name;
	for (auto name_pair : name_index) {
		if (name_pair.second == i) {
			assert(!name);
			name = name_pair.first;
		}
	}
	return name;
}

namespace types {

	/**********************************************************************/
	/* Types                                                              */
	/**********************************************************************/

	std::string type_t::str() const {
		return str(map{});
	}

	std::string type_t::str(const map &bindings) const {
	   	return string_format(c_type("%s"), this->repr(bindings).c_str());
   	}

	atom type_t::repr(const map &bindings) const {
		std::stringstream ss;
		emit(ss, bindings);
		return ss.str();
	}

	type_id_t::type_id_t(identifier::ref id) : id(id) {
	}

	std::ostream &type_id_t::emit(std::ostream &os, const map &bindings) const {
		return os << id->get_name();
	}

	int type_id_t::ftv_count() const {
		/* how many free type variables exist in this type? */
		return 0;
	}

    atom::set type_id_t::get_ftvs() const {
        return {};
    }

	type_t::ref type_id_t::rebind(const map &bindings) const {
		return shared_from_this();
	}

	location_t type_id_t::get_location() const {
		return id->get_location();
	}

	identifier::ref type_id_t::get_id() const {
		return id;
	}

	bool type_id_t::is_void() const {
	   	return id->get_name() == BUILTIN_VOID_TYPE;
   	}

	bool type_id_t::is_nil() const {
	   	return id->get_name() == BUILTIN_NIL_TYPE;
   	}

	type_variable_t::type_variable_t(identifier::ref id) : id(id), location(id->get_location()) {
	}

    identifier::ref gensym() {
        /* generate fresh "any" variables */
        return make_iid({string_format("__%d", next_generic++)});
    }

	type_variable_t::type_variable_t(location_t location) : id(gensym()), location(location) {
	}

	std::ostream &type_variable_t::emit(std::ostream &os, const map &bindings) const {
		auto instance_iter = bindings.find(id->get_name());
		if (instance_iter != bindings.end()) {
			assert(instance_iter->second != shared_from_this());
			return instance_iter->second->emit(os, bindings);
		} else {
			return os << string_format("(any %s)", id->get_name().c_str());
		}
	}

	/* how many free type variables exist in this type? */
	int type_variable_t::ftv_count() const {
		return 1;
	}

    atom::set type_variable_t::get_ftvs() const {
        return {id->get_name()};
    }

	type_t::ref type_variable_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		auto instance_iter = bindings.find(id->get_name());
		if (instance_iter != bindings.end()) {
			return instance_iter->second;
		} else {
			return shared_from_this();
		}
	}

	location_t type_variable_t::get_location() const {
		return location;
	}

	identifier::ref type_variable_t::get_id() const {
		return id;
	}

	type_operator_t::type_operator_t(type_t::ref oper, type_t::ref operand) :
		oper(oper), operand(operand)
	{
	}

	std::ostream &type_operator_t::emit(std::ostream &os, const map &bindings) const {
		oper->emit(os, bindings);
		os << "{";
		operand->emit(os, bindings);
		return os << "}";
	}

	int type_operator_t::ftv_count() const {
		return oper->ftv_count() + operand->ftv_count();
	}

    atom::set type_operator_t::get_ftvs() const {
        atom::set oper_set = oper->get_ftvs();
        atom::set operand_set = operand->get_ftvs();
        oper_set.insert(operand_set.begin(), operand_set.end());
        return oper_set;
    }

	type_t::ref type_operator_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		return ::type_operator(oper->rebind(bindings), operand->rebind(bindings));
	}

	location_t type_operator_t::get_location() const {
		return oper->get_location();
	}

	identifier::ref type_operator_t::get_id() const {
		return oper->get_id();
	}

	type_struct_t::type_struct_t(type_t::refs dimensions, types::name_index_t name_index) :
		dimensions(dimensions), name_index(name_index)
	{
#ifdef ZION_DEBUG
		for (auto dimension: dimensions) {
			assert(dimension != nullptr);
		}
		assert(name_index.size() == dimensions.size() || name_index.size() == 0);
#endif
	}

	product_kind_t type_struct_t::get_pk() const {
		return pk_struct;
	}

	type_t::refs type_struct_t::get_dimensions() const {
		return dimensions;
	}

	name_index_t type_struct_t::get_name_index() const {
		return name_index;
	}

	std::ostream &type_struct_t::emit(std::ostream &os, const map &bindings) const {
		os << "struct[";
		join_dimensions(os, dimensions, name_index, bindings);
		return os << "]";
	}

	int type_struct_t::ftv_count() const {
		int ftv_sum = 0;
		for (auto dimension : dimensions) {
			ftv_sum += dimension->ftv_count();
		}
		return ftv_sum;
	}

	atom::set type_struct_t::get_ftvs() const {
		atom::set set;
		for (auto dimension : dimensions) {
			atom::set dim_set = dimension->get_ftvs();
			set.insert(dim_set.begin(), dim_set.end());
		}
		return set;
    }


	type_t::ref type_struct_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		refs type_dimensions;
		for (auto dimension : dimensions) {
			type_dimensions.push_back(dimension->rebind(bindings));
		}
		return ::type_struct(type_dimensions, name_index);
	}

	location_t type_struct_t::get_location() const {
		if (dimensions.size() != 0) {
			return dimensions[0]->get_location();
		} else {
			return INTERNAL_LOC();
		}
	}

	identifier::ref type_struct_t::get_id() const {
		return nullptr;
	}

	type_args_t::type_args_t(type_t::refs args, types::name_index_t name_index) :
		args(args), name_index(name_index)
	{
#ifdef ZION_DEBUG
		for (auto arg: args) {
			assert(arg != nullptr);
		}
		assert(name_index.size() == args.size() || name_index.size() == 0);
#endif
	}

	product_kind_t type_args_t::get_pk() const {
		return pk_args;
	}

	type_t::refs type_args_t::get_dimensions() const {
		return args;
	}

	name_index_t type_args_t::get_name_index() const {
		return name_index;
	}

	std::ostream &type_args_t::emit(std::ostream &os, const map &bindings) const {
		os << "(";
		const char *sep = "";
		int i = 0;
		for (auto arg : args) {
			os << sep;
			auto name = get_name_from_index(name_index, i++);
			if (!!name) {
				os << name << " ";
			}
			arg->emit(os, bindings);
			sep = ", ";
		}
		return os << ")";
	}

	int type_args_t::ftv_count() const {
		int ftv_sum = 0;
		for (auto arg : args) {
			ftv_sum += arg->ftv_count();
		}
		return ftv_sum;
	}

	atom::set type_args_t::get_ftvs() const {
		atom::set set;
		for (auto arg : args) {
			atom::set dim_set = arg->get_ftvs();
			set.insert(dim_set.begin(), dim_set.end());
		}
		return set;
    }


	type_t::ref type_args_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		refs type_args;
		for (auto arg : args) {
			type_args.push_back(arg->rebind(bindings));
		}
		return ::type_args(type_args, name_index);
	}

	location_t type_args_t::get_location() const {
		if (args.size() != 0) {
			return args[0]->get_location();
		} else {
			return INTERNAL_LOC();
		}
	}

	identifier::ref type_args_t::get_id() const {
		return nullptr;
	}

	type_managed_t::type_managed_t(type_t::ref element_type) :
		element_type(element_type)
	{
#ifdef ZION_DEBUG
		assert(element_type != nullptr);
#endif
	}

	product_kind_t type_managed_t::get_pk() const {
		return pk_managed;
	}

	type_t::refs type_managed_t::get_dimensions() const {
		return {element_type};
	}

	name_index_t type_managed_t::get_name_index() const {
		return {};
	}

	std::ostream &type_managed_t::emit(std::ostream &os, const map &bindings) const {
		os << "managed{";
		element_type->emit(os, bindings);
		os << "}";
		return os;
	}

	int type_managed_t::ftv_count() const {
		return element_type->ftv_count();
	}

	atom::set type_managed_t::get_ftvs() const {
		return element_type->get_ftvs();
    }

	type_t::ref type_managed_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		return ::type_managed(element_type->rebind(bindings));
	}

	location_t type_managed_t::get_location() const {
		return element_type->get_location();
	}

	identifier::ref type_managed_t::get_id() const {
		return element_type->get_id();
	}

	type_module_t::type_module_t(type_t::ref module_type) :
		module_type(module_type)
	{
#ifdef ZION_DEBUG
		assert(module_type != nullptr);
#endif
	}

	product_kind_t type_module_t::get_pk() const {
		return pk_module;
	}

	type_t::refs type_module_t::get_dimensions() const {
		return {module_type};
	}

	name_index_t type_module_t::get_name_index() const {
		return {};
	}

	std::ostream &type_module_t::emit(std::ostream &os, const map &bindings) const {
		os << "module ";
		module_type->emit(os, bindings);
		return os;
	}

	int type_module_t::ftv_count() const {
		return module_type->ftv_count();
	}

	atom::set type_module_t::get_ftvs() const {
		return module_type->get_ftvs();
    }


	type_t::ref type_module_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		return ::type_module(module_type->rebind(bindings));
	}

	location_t type_module_t::get_location() const {
		return module_type->get_location();
	}

	identifier::ref type_module_t::get_id() const {
		return module_type->get_id();
	}

	type_function_t::type_function_t(
			type_t::ref inbound_context,
		   	type_args_t::ref args,
			type_t::ref return_type) :
		inbound_context(inbound_context), args(args), return_type(return_type)
	{
		assert(inbound_context != nullptr);
		assert(args != nullptr);
		assert(return_type != nullptr);
	}

	std::ostream &type_function_t::emit(std::ostream &os, const map &bindings) const {
		os << "[";
		inbound_context->emit(os, bindings);
		os << "] def ";
		args->emit(os, bindings);
		os << " ";
		return return_type->emit(os, bindings);
	}

	int type_function_t::ftv_count() const {
		return args->ftv_count() + return_type->ftv_count();
	}

	atom::set type_function_t::get_ftvs() const {
		atom::set set;
		atom::set args_ftvs = args->get_ftvs();
		set.insert(args_ftvs.begin(), args_ftvs.end());
		atom::set return_type_ftvs = return_type->get_ftvs();
		set.insert(return_type_ftvs.begin(), return_type_ftvs.end());
		return set;
    }


	type_t::ref type_function_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		types::type_args_t::ref rebound_args = dyncast<const types::type_args_t>(
			   	args->rebind(bindings));
		assert(args != nullptr);
		return ::type_function(inbound_context,
				rebound_args, return_type->rebind(bindings));
	}

	location_t type_function_t::get_location() const {
		return args->get_location();
	}

	identifier::ref type_function_t::get_id() const {
		return nullptr;
	}

	bool type_function_t::is_function() const {
	   	return true;
   	}

	type_sum_t::type_sum_t(type_t::refs options) : options(options) {
		for (auto option : options) {
            assert(!dyncast<const type_maybe_t>(option));
            assert(!option->is_nil());
        }
	}

	std::ostream &type_sum_t::emit(std::ostream &os, const map &bindings) const {
		os << "(or";
		assert(options.size() != 0);
		for (auto option : options) {
			os << " ";
			option->emit(os, bindings);
		}
		return os << ")";
	}

	int type_sum_t::ftv_count() const {
		int ftv_sum = 0;
		for (auto option : options) {
			ftv_sum += option->ftv_count();
		}
		return ftv_sum;
	}

    atom::set type_sum_t::get_ftvs() const {
        atom::set set;
		for (auto option : options) {
            atom::set option_set = option->get_ftvs();
            set.insert(option_set.begin(), option_set.end());
		}
		return set;
	}

	type_t::ref type_sum_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		refs type_options;
		for (auto option : options) {
			type_options.push_back(option->rebind(bindings));
		}
		return ::type_sum(type_options);
	}

	location_t type_sum_t::get_location() const {
		if (options.size() != 0) {
			return options[0]->get_location();
		} else {
			return INTERNAL_LOC();
		}
	}

	identifier::ref type_sum_t::get_id() const {
		return nullptr;
	}

	type_maybe_t::type_maybe_t(type_t::ref just) : just(just) {
        assert(!dyncast<const type_maybe_t>(just));
        assert(!dyncast<const type_ref_t>(just));
        assert(!just->is_nil());
	}

	std::ostream &type_maybe_t::emit(std::ostream &os, const map &bindings) const {
        just->emit(os, bindings);
		return os << "?";
	}

	int type_maybe_t::ftv_count() const {
        return just->ftv_count();
	}

    atom::set type_maybe_t::get_ftvs() const {
        return just->get_ftvs();
	}

	type_t::ref type_maybe_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

        return ::type_maybe(just->rebind(bindings));
	}

	location_t type_maybe_t::get_location() const {
        return just->get_location();
	}

	identifier::ref type_maybe_t::get_id() const {
		return nullptr;
	}

	type_ptr_t::type_ptr_t(type_t::ref element_type) : element_type(element_type) {
		assert(!element_type->is_nil());
	}

	std::ostream &type_ptr_t::emit(std::ostream &os, const map &bindings) const {
		os << "*";
		element_type->emit(os, bindings);
		return os;
	}

	int type_ptr_t::ftv_count() const {
		return element_type->ftv_count();
	}

	atom::set type_ptr_t::get_ftvs() const {
		return element_type->get_ftvs();
	}

	type_t::ref type_ptr_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		return ::type_ptr(element_type->rebind(bindings));
	}

	location_t type_ptr_t::get_location() const {
		return element_type->get_location();
	}

	identifier::ref type_ptr_t::get_id() const {
		return nullptr;
	}

	type_ref_t::type_ref_t(type_t::ref element_type) : element_type(element_type) {
		assert(!element_type->is_nil());
	}

	std::ostream &type_ref_t::emit(std::ostream &os, const map &bindings) const {
		os << "&";
		element_type->emit(os, bindings);
		return os;
	}

	int type_ref_t::ftv_count() const {
		return element_type->ftv_count();
	}

	atom::set type_ref_t::get_ftvs() const {
		return element_type->get_ftvs();
	}

	type_t::ref type_ref_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		return ::type_ref(element_type->rebind(bindings));
	}

	location_t type_ref_t::get_location() const {
		return element_type->get_location();
	}

	identifier::ref type_ref_t::get_id() const {
		return nullptr;
	}

	type_lambda_t::type_lambda_t(identifier::ref binding, type_t::ref body) :
		binding(binding), body(body)
	{
	}

	std::ostream &type_lambda_t::emit(std::ostream &os, const map &bindings_) const {
		os << "(lambda [" << binding->get_name() << "] ";
		map bindings = bindings_;
		auto binding_iter = bindings.find(binding->get_name());
		if (binding_iter != bindings.end()) {
			bindings.erase(binding_iter);
		}
		body->emit(os, bindings);
		return os << ")";
	}

	int type_lambda_t::ftv_count() const {
		/* pretend this is getting applied */
		assert(!"This should not really get called ....");
		map bindings;
		bindings[binding->get_name()] = type_unreachable();
		return body->rebind(bindings)->ftv_count();
	}

    atom::set type_lambda_t::get_ftvs() const {
		assert(!"This should not really get called ....");
		map bindings;
		bindings[binding->get_name()] = type_unreachable();
		return body->rebind(bindings)->get_ftvs();
	}

	type_t::ref type_lambda_t::rebind(const map &bindings_) const {
		if (bindings_.size() == 0) {
			return shared_from_this();
		}

		map bindings = bindings_;
		auto binding_iter = bindings.find(binding->get_name());
		if (binding_iter != bindings.end()) {
			bindings.erase(binding_iter);
		}
		return ::type_lambda(binding, body->rebind(bindings));
	}

	location_t type_lambda_t::get_location() const {
		return binding->get_location();
	}

	identifier::ref type_lambda_t::get_id() const {
		return nullptr;
	}

	bool is_type_id(type_t::ref type, atom type_name) {
		if (auto pti = dyncast<const types::type_id_t>(type)) {
			return pti->id->get_name() == type_name;
		}
		return false;
	}

	bool is_managed_ptr(types::type_t::ref type, types::type_t::map env) {
		if (auto maybe_type = dyncast<const types::type_maybe_t>(type)) {
			type = maybe_type->just;
		}

		if (auto expanded_type = eval(type, env)) {
			type = expanded_type;
		}

		if (auto ptr_type = dyncast<const types::type_ptr_t>(type)) {
			if (dyncast<const types::type_managed_t>(ptr_type->element_type)) {
				return true;
			}
		}

		if (auto ptr_type = dyncast<const types::type_sum_t>(type)) {
			/* sum types are always managed pointers for now */
			return true;
		}
		return false;
	}

	bool is_ptr(types::type_t::ref type, types::type_t::map env) {
		if (auto maybe_type = dyncast<const types::type_maybe_t>(type)) {
			type = maybe_type->just;
		}

		if (auto expanded_type = eval(type, env)) {
			type = expanded_type;
		}

		if (auto ptr_type = dyncast<const types::type_ptr_t>(type)) {
			return true;
		}

		if (auto ptr_type = dyncast<const types::type_sum_t>(type)) {
			/* sum types are always managed pointers for now */
			return true;
		}
		return false;
	}
}

types::type_t::ref type_id(identifier::ref id) {
	return make_ptr<types::type_id_t>(id);
}

types::type_t::ref type_variable(identifier::ref id) {
	return make_ptr<types::type_variable_t>(id);
}

types::type_t::ref type_variable(location_t location) {
	return make_ptr<types::type_variable_t>(location);
}

types::type_t::ref type_unreachable() {
	return make_ptr<types::type_id_t>(make_iid(BUILTIN_UNREACHABLE_TYPE));
}

types::type_t::ref type_nil() {
	static auto nil_type = make_ptr<types::type_id_t>(make_iid(BUILTIN_NIL_TYPE));
    return nil_type;
}

types::type_t::ref type_void() {
	return make_ptr<types::type_id_t>(make_iid(BUILTIN_VOID_TYPE));
}

types::type_t::ref type_operator(types::type_t::ref operator_, types::type_t::ref operand) {
	return make_ptr<types::type_operator_t>(operator_, operand);
}

types::type_struct_t::ref type_struct(
	   	types::type_t::refs dimensions,
	   	types::name_index_t name_index)
{
	if (name_index.size() == 0) {
		/* if we omit names for our dimensions, give them names like _0, _1, _2,
		 * etc... so they can be accessed like mytuple._5 if necessary */
		for (size_t i = 0; i < dimensions.size(); ++i) {
			name_index[string_format("_%d", i)] = i;
		}
	}
	return make_ptr<types::type_struct_t>(dimensions, name_index);
}

types::type_args_t::ref type_args(
	   	types::type_t::refs args,
	   	types::name_index_t name_index)
{
	return make_ptr<types::type_args_t>(args, name_index);
}

types::type_module_t::ref type_module(types::type_t::ref module_type) {
	return make_ptr<types::type_module_t>(module_type);
}

types::type_managed_t::ref type_managed(types::type_t::ref element_type) {
	return make_ptr<types::type_managed_t>(element_type);
}

types::type_function_t::ref type_function(
		types::type_t::ref inbound_context,
		types::type_args_t::ref args,
		types::type_t::ref return_type)
{
	return make_ptr<types::type_function_t>(inbound_context, args, return_type);
}

types::type_t::ref type_sum_safe(status_t &status, types::type_t::refs options) {
	/* sum types must take care to avoid creating sums over maybe types and over
	 * builtin types */
	bool make_maybe = false;
	types::type_t::refs safe_options;
	for (auto option : options) {
		if (auto maybe = dyncast<const types::type_maybe_t>(option)) {
			make_maybe = true;
			option = maybe->just;
		}
		
		safe_options.push_back(option);
	}

	auto ret = type_sum(safe_options);
	if (make_maybe) {
		/* lift the maybe-ness of one of the inner types up to the whole
		 * type */
		return type_maybe(ret);
	} else {
		return ret;
	}
}

types::type_t::ref type_sum(types::type_t::refs options) {
	return make_ptr<types::type_sum_t>(options);
}

types::type_t::ref type_maybe(types::type_t::ref just) {
    if (auto maybe = dyncast<const types::type_maybe_t>(just)) {
		return just;
	}
	return make_ptr<types::type_maybe_t>(just);
}

types::type_t::ref type_ptr(types::type_t::ref raw) {
	return make_ptr<types::type_ptr_t>(raw);
}

types::type_t::ref type_ref(types::type_t::ref raw) {
	return make_ptr<types::type_ref_t>(raw);
}

types::type_t::ref type_lambda(identifier::ref binding, types::type_t::ref body) {
	return make_ptr<types::type_lambda_t>(binding, body);
}

types::type_t::ref type_list_type(types::type_t::ref element) {
	return type_maybe(type_operator(type_id(make_iid_impl(
						STD_LIST_TYPE, element->get_location())), element));
}

types::type_t::ref type_strip_maybe(types::type_t::ref maybe_maybe) {
    if (auto maybe = dyncast<const types::type_maybe_t>(maybe_maybe)) {
        return maybe->just;
    } else {
        return maybe_maybe;
    }
}

std::ostream& operator <<(std::ostream &os, const types::type_t::ref &type) {
	os << type->str();
	return os;
}

types::type_t::ref get_function_return_type(types::type_t::ref function_type) {
	debug_above(5, log(log_info, "getting function return type from %s", function_type->str().c_str()));

	auto type_function = dyncast<const types::type_function_t>(function_type);
	assert(type_function != nullptr);

	return type_function->return_type;
}

std::ostream &operator <<(std::ostream &os, identifier::ref id) {
	return os << id->get_name();
}

types::type_t::pair make_type_pair(std::string fst, std::string snd, identifier::set generics) {
	debug_above(4, log(log_info, "creating type pair with (%s, %s) and generics [%s]",
				fst.c_str(), snd.c_str(),
			   	join(generics, ", ").c_str()));

	auto module_id = make_iid("tests");
	return types::type_t::pair{
		parse_type_expr(fst, generics, module_id),
	   	parse_type_expr(snd, generics, module_id)};
}

types::type_t::ref parse_type_expr(std::string input, identifier::set generics, identifier::ref module_id) {
	status_t status;
	std::istringstream iss(input);
	zion_lexer_t lexer("", iss);
	parse_state_t ps(status, "", lexer, {}, nullptr);
	if (module_id != nullptr) {
		ps.module_id = module_id;
	} else {
		ps.module_id = make_iid("__parse_type_expr__");
	}
	types::type_t::ref type = parse_maybe_type(ps, {}, {}, generics);
	if (!!status) {
		return type;
	} else {
		panic("bad type");
		return null_impl();
	}
}

bool get_type_variable_name(types::type_t::ref type, atom &name) {
    if (auto ptv = dyncast<const types::type_variable_t>(type)) {
		name = ptv->id->get_name();
		return true;
	} else {
		return false;
	}
	return false;
}

std::string str(types::type_t::refs refs) {
	std::stringstream ss;
	ss << "(";
	const char *sep = "";
	for (auto p : refs) {
		ss << sep << p->str();
		sep = ", ";
	}
	ss << ")";
	return ss.str();
}

std::string str(types::type_t::map coll) {
	std::stringstream ss;
	ss << "{";
	const char *sep = "";
	for (auto p : coll) {
		ss << sep << C_ID << p.first.c_str() << C_RESET ": ";
		ss << p.second->str().c_str();
		sep = ", ";
	}
	ss << "}";
	return ss.str();
}

const char *pkstr(product_kind_t pk) {
	switch (pk) {
	case pk_module:
		return "module";
	case pk_struct:
		return "struct";
	case pk_managed:
		return "managed";
	case pk_args:
		return "args";
	}
	assert(false);
	return nullptr;
}

types::type_t::ref eval(types::type_t::ref type, types::type_t::map env) {
	/* if there is no expansion of the type passed in, we will return nullptr */
	debug_above(7, log("eval'ing %s in %s",
				type->str().c_str(),
				str(env).c_str()));
	if (auto id = dyncast<const types::type_id_t>(type)) {
		return eval_id(id, env);
	} else if (auto operator_ = dyncast<const types::type_operator_t>(type)) {
		return eval_apply(operator_->oper, operator_->operand, env);
	} else if (auto pointer = dyncast<const types::type_ptr_t>(type)) {
		auto evaled = eval(pointer->element_type, env);
		if (evaled != nullptr) {
			return type_ptr(evaled);
		} else {
			return nullptr;
		}
	} else if (auto ref = dyncast<const types::type_ref_t>(type)) {
		auto evaled = eval(ref->element_type, env);
		if (evaled != nullptr) {
			return type_ref(evaled);
		} else {
			return nullptr;
		}
	} else if (auto struct_type = dyncast<const types::type_struct_t>(type)) {
		/* there is no expansion of struct types */
		return nullptr;
	} else if (auto managed_type = dyncast<const types::type_managed_t>(type)) {
		/* there is no expansion of managed types, since they are fully concrete */
		return nullptr;
	} else if (auto sum_type = dyncast<const types::type_sum_t>(type)) {
		/* there is no expansion of sum types */
		return nullptr;
	} else {
		log("unhandled type evaluation for type %s in env %s",
				type->str().c_str(),
				str(env).c_str());
		return null_impl();
	}
}

types::type_t::ref eval_id(
		ptr<const types::type_id_t> ptid,
		types::type_t::map env)
{
	/* if there is no expansion of the type passed in, we will return nullptr */

	assert(ptid != nullptr);

	/* look in the environment for a declaration of this term */
	auto fn_iter = env.find(ptid->id->get_name());
	if (fn_iter != env.end()) {
		return fn_iter->second;
	} else {
		return nullptr;
	}
}

types::type_t::ref eval_apply(
		types::type_t::ref oper,
	   	types::type_t::ref operand, 
		types::type_t::map env)
{
	/* if there is no expansion of the type passed in, we will return nullptr */

	assert(oper != nullptr);
	assert(operand != nullptr);
	if (auto ptid = dyncast<const types::type_id_t>(oper)) {
		/* look in the environment for a declaration of this operator */
		types::type_t::ref expansion = eval_id(ptid, env);

		debug_above(7, log(log_info, "eval_apply : %s expanded to %s in %s",
					ptid->str().c_str(),
					((expansion != nullptr) ? expansion->str().c_str() : c_error("nothing")),
                    str(env).c_str()));

		if (expansion != nullptr) {
			return eval_apply(expansion, operand, env);
		} else {
			return nullptr;
		}
	} else if (auto lambda = dyncast<const types::type_lambda_t>(oper)) {
		auto var_name = lambda->binding->get_name();
		return lambda->body->rebind({{var_name, operand}});
	} else if (auto pto = dyncast<const types::type_operator_t>(oper)) {
		auto new_operator = eval_apply(pto->oper, pto->operand, env);
		if (new_operator != nullptr) {
			return eval_apply(new_operator, operand, env);
		} else {
			return nullptr;
		}
	} else if (auto ptv = dyncast<const types::type_variable_t>(oper)) {
		/* type_variables cannot be applied */
		return nullptr;
	} else if (auto pts = dyncast<const types::type_sum_t>(oper)) {
		/* type_variables cannot be applied */
		return nullptr;
	} else {
		/* other strange oddities are not applicable */
		return nullptr;
	}
}

std::ostream &join_dimensions(std::ostream &os, const types::type_t::refs &dimensions, const types::name_index_t &name_index, const types::type_t::map &bindings) {
	const char *sep = "";
	int i = 0;
	for (auto dimension : dimensions) {
		os << sep;
		auto name = get_name_from_index(name_index, i++);
		if (!!name) {
			os << name << " ";
		}
		dimension->emit(os, bindings);
		sep = ", ";
	}
	return os;
}
