#include "zion.h"
#include "dbg.h"
#include "types.h"
#include <sstream>
#include "utils.h"
#include "types.h"
#include "type_parser.h"
#include <iostream>
#include "unification.h"
#include "atom.h"
#include "scopes.h"
#include "encoding.h"

const char *NULL_TYPE = "null";
const char *STD_MANAGED_TYPE = "var_t";
const char *STD_VECTOR_TYPE = "vector.Vector";
const char *STD_MAP_TYPE = "map.Map";
const char *VOID_TYPE = "void";
const char *BOTTOM_TYPE = "⊥";

const char *TYPE_OP_NOT = "not";
const char *TYPE_OP_IF = "if";
const char *TYPE_OP_GC = "gc";
const char *TYPE_OP_IS_ZERO = "is_zero";
const char *TYPE_OP_IS_REF = "is_ref";
const char *TYPE_OP_IS_TRUE = "is_true";
const char *TYPE_OP_IS_FALSE = "is_false";
const char *TYPE_OP_IS_BOOL = "is_bool";
const char *TYPE_OP_IS_POINTER = "is_pointer";
const char *TYPE_OP_IS_FUNCTION = "is_function";
const char *TYPE_OP_IS_VOID = "is_void";
const char *TYPE_OP_IS_UNIT = "is_unit";
const char *TYPE_OP_IS_NULL = "is_null";
const char *TYPE_OP_IS_INT = "is_int";
const char *TYPE_OP_IS_MAYBE = "is_maybe";

int next_generic = 1;

void reset_generics() {
	next_generic = 1;
}

std::string get_name_from_index(const types::name_index_t &name_index, int i) {
	std::string name;
	for (auto name_pair : name_index) {
		if (name_pair.second == i) {
			assert(name.size() == 0);
			name = name_pair.first;
		}
	}
	return name;
}

struct parens_t {
	std::ostream &os;
	const int parent_precedence, child_precedence;
	parens_t(std::ostream &os, int parent_precedence, int child_precedence) : os(os), parent_precedence(parent_precedence), child_precedence(child_precedence) {
		if (parent_precedence > child_precedence) {
			os << "(";
		}
	}
	~parens_t() {
		if (parent_precedence > child_precedence) {
			os << ")";
		}
	}
};

namespace types {

	/**********************************************************************/
	/* Types                                                              */
	/**********************************************************************/

	std::string type_t::str() const {
		return str(map{});
	}

	void type_t::encode(env_t::ref env, std::vector<uint16_t> &encoding) const {
		assert(eval(env) == shared_from_this());
		encoding.push_back(atomize(repr()));
		// throw user_error(get_location(), "unable to encode type %s", str().c_str());
	}

	std::string type_t::str(const map &bindings) const {
		return string_format(c_type("%s"), this->repr(bindings).c_str());
	}

	std::string type_t::repr(const map &bindings) const {
		std::stringstream ss;
		emit(ss, bindings, 0);
		return ss.str();
	}

	type_t::ref type_t::boolean_refinement(
			bool elimination_value,
			env_t::ref env) const
	{
		return shared_from_this();
	}

	type_id_t::type_id_t(identifier::ref id) : id(id) {
	}

	std::ostream &type_id_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		return os << id->get_name();
	}

	void type_id_t::encode(env_t::ref env, std::vector<uint16_t> &encoding) const {
		encoding.push_back(atomize(id->get_name()));
	}

	int type_id_t::ftv_count() const {
		/* how many free type variables exist in this type? */
		return 0;
	}

	std::set<std::string> type_id_t::get_ftvs() const {
		return {};
	}

	type_t::ref type_id_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		return shared_from_this();
	}

	location_t type_id_t::get_location() const {
		return id->get_location();
	}

	type_t::ref type_id_t::boolean_refinement(
			bool elimination_value,
			env_t::ref env) const
	{
		debug_above(6, log("refining %s. looking to eliminate %s values from the type", str().c_str(), boolstr(elimination_value)));
		auto evaled = eval_core(env, false /*get_structural_type*/);
		if (auto id_type = dyncast<const type_id_t>(evaled)) {
			auto name = id_type->id->get_name();
			if (name == NULL_TYPE) {
				if (elimination_value) {
					debug_above(6, log("keeping %s", str().c_str()));
					return shared_from_this();
				} else {
					debug_above(6, log("eliding the whole type of %s", str().c_str()));
					return nullptr;
				}
			}

			/* handle builtin type ids */
			if (name == boolstr(elimination_value)) {
				debug_above(6, log("eliding the whole type of %s", str().c_str()));
				return nullptr;
			} else if (name == BOOL_TYPE) {
				auto refinement = type_id(make_iid_impl(boolstr(!elimination_value), get_location()));
				debug_above(6, log("refining %s to %s", str().c_str(), refinement->str().c_str()));
				return refinement;
			}
		}

		return shared_from_this();
	}

	type_variable_t::type_variable_t(identifier::ref id) : id(id), location(id->get_location()) {
	}

	identifier::ref gensym() {
		/* generate fresh "any" variables */
		return make_iid({string_format("__%d", next_generic++)});
	}

	type_variable_t::type_variable_t(location_t location) : id(gensym()), location(location) {
	}

	std::ostream &type_variable_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		auto instance_iter = bindings.find(id->get_name());
		if (instance_iter != bindings.end()) {
			assert(instance_iter->second != shared_from_this());
			return instance_iter->second->emit(os, bindings, parent_precedence);
		} else {
			return os << string_format("any %s", id->get_name().c_str());
		}
	}

	/* how many free type variables exist in this type? */
	int type_variable_t::ftv_count() const {
		return 1;
	}

	std::set<std::string> type_variable_t::get_ftvs() const {
		return {id->get_name()};
	}

	type_t::ref type_variable_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() != 0) {
			auto instance_iter = bindings.find(id->get_name());
			if (instance_iter != bindings.end()) {
				/* recurse the rebinding, but remove the current rebinding */
				map new_bindings;
				for (auto &pair : bindings) {
					if (pair.first == id->get_name()) {
						continue;
					}
					new_bindings.insert(pair);
				}
				return instance_iter->second->rebind(new_bindings);
			}
		}
		return bottom_out_free_vars ? type_bottom() : shared_from_this();
	}

	location_t type_variable_t::get_location() const {
		return location;
	}

	type_operator_t::type_operator_t(type_t::ref oper, type_t::ref operand) :
		oper(oper), operand(operand)
	{
	}

	std::ostream &type_operator_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		if (is_type_id(oper->rebind(bindings), STD_VECTOR_TYPE, nullptr)) {
			os << "[";
			operand->emit(os, bindings, 0);
			return os << "]";
		} else {
			parens_t parens(os, parent_precedence, get_precedence());
			oper->emit(os, bindings, get_precedence());
			os << " ";
			operand->emit(os, bindings, get_precedence() + 1);
			return os;
		}
	}

	void type_operator_t::encode(env_t::ref env, std::vector<uint16_t> &encoding) const {
		static int depth = 0;
		depth_guard_t depth_guard(get_location(), depth, 10);

		encoding.push_back(APPLY_INST);
		oper->eval(env)->encode(env, encoding);
		operand->eval(env)->encode(env, encoding);
	}

	int type_operator_t::ftv_count() const {
		return oper->ftv_count() + operand->ftv_count();
	}

	std::set<std::string> type_operator_t::get_ftvs() const {
		std::set<std::string> oper_set = oper->get_ftvs();
		std::set<std::string> operand_set = operand->get_ftvs();
		oper_set.insert(operand_set.begin(), operand_set.end());
		return oper_set;
	}

	type_t::ref type_operator_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0 && !bottom_out_free_vars) {
			return shared_from_this();
		}

		return ::type_operator(oper->rebind(bindings, bottom_out_free_vars), operand->rebind(bindings, bottom_out_free_vars));
	}

	location_t type_operator_t::get_location() const {
		return oper->get_location();
	}

	type_t::ref type_operator_t::boolean_refinement(bool elimination_value, env_t::ref env) const {
		auto expansion = eval(env);
		if (expansion != nullptr) {
			/* refine the expanded version of this type */
			auto refined_expansion = expansion->boolean_refinement(elimination_value, env);
			if (refined_expansion == nullptr) {
				/* if the refinement results in elimination, so be it */
				return nullptr;
			} else if (refined_expansion->get_signature() == expansion->get_signature()) {
				/* if the refinement does nothing, return the original type */
				return shared_from_this();
			} else {
				/* the refinement changed something, so return that */
				return refined_expansion;
			}
		} else {
			/* there is no expansion, so just return this type because we don't know how to refine
			 * it */
			debug_above(6, log("no boolean refinement available for %s", str().c_str()));
			return shared_from_this();
		}
	}

	type_subtype_t::type_subtype_t(type_t::ref lhs, type_t::ref rhs) :
		lhs(lhs), rhs(rhs)
	{
	}

	std::ostream &type_subtype_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		parens_t parens(os, parent_precedence, get_precedence());

		lhs->emit(os, bindings, get_precedence());
		os << " <: ";
		return rhs->emit(os, bindings, get_precedence());
	}

	int type_subtype_t::ftv_count() const {
		return lhs->ftv_count() + rhs->ftv_count();
	}

	std::set<std::string> type_subtype_t::get_ftvs() const {
		std::set<std::string> lhs_set = lhs->get_ftvs();
		std::set<std::string> rhs_set = rhs->get_ftvs();
		lhs_set.insert(rhs_set.begin(), rhs_set.end());
		return lhs_set;
	}

	type_t::ref type_subtype_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0 && !bottom_out_free_vars) {
			return shared_from_this();
		}

		return ::type_subtype(lhs->rebind(bindings, bottom_out_free_vars), rhs->rebind(bindings, bottom_out_free_vars));
	}

	location_t type_subtype_t::get_location() const {
		return lhs->get_location();
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

	std::ostream &type_struct_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		os << "struct{";
		join_dimensions(os, dimensions, name_index, bindings);
		return os << "}";
	}

	int type_struct_t::ftv_count() const {
		int ftv_sum = 0;
		for (auto dimension : dimensions) {
			ftv_sum += dimension->ftv_count();
		}
		return ftv_sum;
	}

	std::set<std::string> type_struct_t::get_ftvs() const {
		std::set<std::string> set;
		for (auto dimension : dimensions) {
			std::set<std::string> dim_set = dimension->get_ftvs();
			set.insert(dim_set.begin(), dim_set.end());
		}
		return set;
	}


	type_t::ref type_struct_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0 && !bottom_out_free_vars) {
			return shared_from_this();
		}

		bool anything_was_rebound = false;
		refs type_dimensions;
		for (auto dimension : dimensions) {
			auto new_dim = dimension->rebind(bindings, bottom_out_free_vars);
			if (new_dim != dimension) {
				anything_was_rebound = true;
			}
			type_dimensions.push_back(new_dim);
		}

		if (anything_was_rebound) {
			return ::type_struct(type_dimensions, name_index);
		} else {
			return shared_from_this();
		}
	}

	location_t type_struct_t::get_location() const {
		if (dimensions.size() != 0) {
			return dimensions[0]->get_location();
		} else {
			return INTERNAL_LOC();
		}
	}

	type_tuple_t::type_tuple_t(type_t::refs dimensions) :
		dimensions(dimensions)
	{
#ifdef ZION_DEBUG
		for (auto dimension: dimensions) {
			assert(dimension != nullptr);
		}
#endif
	}

	product_kind_t type_tuple_t::get_pk() const {
		return pk_tuple;
	}

	type_t::refs type_tuple_t::get_dimensions() const {
		return dimensions;
	}

	std::ostream &type_tuple_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		os << "(";
		join_dimensions(os, dimensions, {}, bindings);
		return os << ")";
	}

	int type_tuple_t::ftv_count() const {
		int ftv_sum = 0;
		for (auto dimension : dimensions) {
			ftv_sum += dimension->ftv_count();
		}
		return ftv_sum;
	}

	std::set<std::string> type_tuple_t::get_ftvs() const {
		std::set<std::string> set;
		for (auto dimension : dimensions) {
			std::set<std::string> dim_set = dimension->get_ftvs();
			set.insert(dim_set.begin(), dim_set.end());
		}
		return set;
	}


	type_t::ref type_tuple_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0 && !bottom_out_free_vars) {
			return shared_from_this();
		}

		bool anything_was_rebound = false;
		refs type_dimensions;
		for (auto dimension : dimensions) {
			auto new_dim = dimension->rebind(bindings, bottom_out_free_vars);
			if (new_dim != dimension) {
				anything_was_rebound = true;
			}
			type_dimensions.push_back(new_dim);
		}

		if (anything_was_rebound) {
			return ::type_tuple(type_dimensions);
		} else {
			return shared_from_this();
		}
	}

	location_t type_tuple_t::get_location() const {
		if (dimensions.size() != 0) {
			return dimensions[0]->get_location();
		} else {
			return INTERNAL_LOC();
		}
	}

	type_args_t::type_args_t(type_t::refs args, identifier::refs names) :
		args(args), names(names)
	{
#ifdef ZION_DEBUG
		for (auto arg: args) {
			assert(arg != nullptr);
		}
		assert(names.size() == args.size() || names.size() == 0);
#endif
	}

	product_kind_t type_args_t::get_pk() const {
		return pk_args;
	}

	type_t::refs type_args_t::get_dimensions() const {
		return args;
	}

	std::ostream &type_args_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		os << "(";
		const char *sep = "";
		// int i = 0;
		for (auto arg : args) {
			os << sep;
#if 0
			if (names.size() != 0) {
				auto name = names[i++]->get_name();
				if (name.size() != 0) {
					os << name << " ";
				}
			}
#endif
			arg->emit(os, bindings, 0);
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

	std::set<std::string> type_args_t::get_ftvs() const {
		std::set<std::string> set;
		for (auto arg : args) {
			std::set<std::string> dim_set = arg->get_ftvs();
			set.insert(dim_set.begin(), dim_set.end());
		}
		return set;
	}


	type_t::ref type_args_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0 && !bottom_out_free_vars) {
			return shared_from_this();
		}

		refs type_args;
		for (auto arg : args) {
			type_args.push_back(arg->rebind(bindings, bottom_out_free_vars));
		}
		return ::type_args(type_args, names);
	}

	location_t type_args_t::get_location() const {
		if (args.size() != 0) {
			return args[0]->get_location();
		} else {
			return INTERNAL_LOC();
		}
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


	std::ostream &type_managed_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		os << "managed{";
		element_type->emit(os, bindings, 0);
		os << "}";
		return os;
	}

	int type_managed_t::ftv_count() const {
		return element_type->ftv_count();
	}

	std::set<std::string> type_managed_t::get_ftvs() const {
		return element_type->get_ftvs();
	}

	type_t::ref type_managed_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0 && !bottom_out_free_vars) {
			return shared_from_this();
		}

		return ::type_managed(element_type->rebind(bindings, bottom_out_free_vars));
	}

	location_t type_managed_t::get_location() const {
		return element_type->get_location();
	}

	type_injection_t::type_injection_t(type_t::ref module_type) :
		module_type(module_type)
	{
#ifdef ZION_DEBUG
		assert(module_type != nullptr);
#endif
	}

	product_kind_t type_injection_t::get_pk() const {
		return pk_module;
	}

	type_t::refs type_injection_t::get_dimensions() const {
		return {module_type};
	}

	std::ostream &type_injection_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		os << "module ";
		module_type->emit(os, bindings, get_precedence());
		return os;
	}

	int type_injection_t::ftv_count() const {
		return module_type->ftv_count();
	}

	std::set<std::string> type_injection_t::get_ftvs() const {
		return module_type->get_ftvs();
	}


	type_t::ref type_injection_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0 && !bottom_out_free_vars) {
			return shared_from_this();
		}

		return ::type_injection(module_type->rebind(bindings, bottom_out_free_vars));
	}

	location_t type_injection_t::get_location() const {
		return module_type->get_location();
	}

	type_function_t::type_function_t(
			location_t location,
			types::type_t::ref type_constraints,
			types::type_t::ref args,
			type_t::ref return_type) :
		location(location),
		type_constraints(type_constraints),
		args(args), return_type(return_type)
	{
		assert(args != nullptr);
		// assert(dyncast<const type_args_t>(args) != nullptr || dyncast<const type_variable_t>(args) != nullptr);
		// assert(return_type != nullptr);
	}

	std::ostream &type_function_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		os << K(fn) << " ";
#if 0
		if (name != nullptr) {
			os << C_ID << name->get_name() << C_RESET;
		}
#endif
		if (type_constraints != nullptr) {
			os << "[" << C_CONTROL << "where " << C_RESET;
			type_constraints->emit(os, bindings, 0);
			os << "]";
		}
		os << "_";
		args->emit(os, bindings, 0);
		os << " ";
		return return_type->emit(os, bindings, 0);
	}

	int type_function_t::ftv_count() const {
		return args->ftv_count() + return_type->ftv_count();
	}

	std::set<std::string> type_function_t::get_ftvs() const {
		std::set<std::string> set;
		std::set<std::string> args_ftvs = args->get_ftvs();
		set.insert(args_ftvs.begin(), args_ftvs.end());
		std::set<std::string> return_type_ftvs = return_type->get_ftvs();
		set.insert(return_type_ftvs.begin(), return_type_ftvs.end());
		return set;
	}

	type_t::ref type_function_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0 && !bottom_out_free_vars) {
			return shared_from_this();
		}

		types::type_t::ref rebound_args = args->rebind(bindings, bottom_out_free_vars);
		assert(rebound_args != nullptr);
		if (rebound_args == args) {
			return shared_from_this();
		}
		return ::type_function(
				get_location(),
				type_constraints != nullptr ? type_constraints->rebind(bindings, bottom_out_free_vars) : type_constraints,
				rebound_args,
				return_type->rebind(bindings, bottom_out_free_vars));
	}

	location_t type_function_t::get_location() const {
		return location;
	}

	type_function_closure_t::type_function_closure_t(type_t::ref function) : function(function) {
	}

	std::ostream &type_function_closure_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		os << "bound(";
		function->emit(os, bindings, parent_precedence);
		return os << ")";
	}

	int type_function_closure_t::ftv_count() const {
		return function->ftv_count();
	}

	std::set<std::string> type_function_closure_t::get_ftvs() const {
		return function->get_ftvs();
	}

	type_t::ref type_function_closure_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		return ::type_function_closure(function->rebind(bindings, bottom_out_free_vars));
	}

	location_t type_function_closure_t::get_location() const {
		return function->get_location();
	}


	type_and_t::type_and_t(type_t::refs terms) : terms(terms) {
		for (auto term : terms) {
			assert(!dyncast<const type_maybe_t>(term));
		}
	}

	std::ostream &type_and_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		parens_t parens(os, parent_precedence, get_precedence());
		const char *delim = "";
		assert(terms.size() != 0);
		for (auto term : terms) {
			os << delim;
			term->emit(os, bindings, get_precedence());
			delim = " and ";
		}
		return os;
	}

	int type_and_t::ftv_count() const {
		int ftv_sum = 0;
		for (auto term : terms) {
			ftv_sum += term->ftv_count();
		}
		return ftv_sum;
	}

	std::set<std::string> type_and_t::get_ftvs() const {
		std::set<std::string> set;
		for (auto term : terms) {
			std::set<std::string> option_set = term->get_ftvs();
			set.insert(option_set.begin(), option_set.end());
		}
		return set;
	}

	type_t::ref type_and_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0 && !bottom_out_free_vars) {
			return shared_from_this();
		}

		refs type_options;
		for (auto term : terms) {
			type_options.push_back(term->rebind(bindings, bottom_out_free_vars));
		}
		return ::type_and(type_options);
	}

	location_t type_and_t::get_location() const {
		return location;
	}

	const token_kind type_eq_t::TK = tk_binary_equal;

	type_eq_t::type_eq_t(type_t::ref lhs, type_t::ref rhs, location_t location) :
		lhs(lhs), rhs(rhs), location(location) {
		}

	std::ostream &type_eq_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		parens_t parens(os, parent_precedence, get_precedence());
		lhs->emit(os, bindings, get_precedence());
		os << " " << tkstr(type_eq_t::TK) << " ";
		return rhs->emit(os, bindings, get_precedence());
	}

	int type_eq_t::ftv_count() const {
		return lhs->ftv_count() + rhs->ftv_count();
	}

	std::set<std::string> type_eq_t::get_ftvs() const {
		std::set<std::string> set = lhs->get_ftvs();
		std::set<std::string> rhs_set = rhs->get_ftvs();
		set.insert(rhs_set.begin(), rhs_set.end());
		return set;
	}

	type_t::ref type_eq_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0 && !bottom_out_free_vars) {
			return shared_from_this();
		}

		return ::type_eq(lhs->rebind(bindings, bottom_out_free_vars), rhs->rebind(bindings, bottom_out_free_vars), location);
	}

	location_t type_eq_t::get_location() const {
		return location;
	}

	type_maybe_t::type_maybe_t(type_t::ref just) : just(just) {
		assert(!dyncast<const type_maybe_t>(just));
		assert(!dyncast<const type_ref_t>(just));
		// TODO: revisit this... lazy types...
		// assert(!just->is_null());
	}

	std::ostream &type_maybe_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		if (auto pointer = dyncast<const type_ptr_t>(just)) {
			/* this is a native pointer that might be null */
			os << "*?";
			return pointer->element_type->emit(os, bindings, get_precedence());
		} else {
			/* this is a managed pointer that might be null. we subsume the maybeness onto the whole typename in order
			 * to look nicer. */
			just->emit(os, bindings, get_precedence());
			return os << "?";
		}
	}

	int type_maybe_t::ftv_count() const {
		return just->ftv_count();
	}

	std::set<std::string> type_maybe_t::get_ftvs() const {
		return just->get_ftvs();
	}

	type_t::ref type_maybe_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0 && !bottom_out_free_vars) {
			return shared_from_this();
		}

		// NOTE: this may fail because i have not plumbed through the env... probably
		// can bypass the ptr check here
		return ::type_maybe(just->rebind(bindings, bottom_out_free_vars), {});
	}

	location_t type_maybe_t::get_location() const {
		return just->get_location();
	}

	type_t::ref type_maybe_t::boolean_refinement(
			bool elimination_value,
			env_t::ref env) const
	{
		auto just_refined = just->boolean_refinement(elimination_value, env);
		if (!elimination_value) {
			/* we are eliminating falseyness, so we can eliminate the maybeness, too */
			return just_refined;
		} else {
			if (just_refined != just) {
				/* eliminate truthyness. the just refinement returned a new object, so let's construct a maybe around
				 * it, since we can not eliminate the maybe when we are eliminating truthyness */
				// NB: no env here, prolly need to bypass
				return ::type_maybe(just_refined, {});
			} else {
				/* nothing learned from this refinement */
				return shared_from_this();
			}
		}
	}

	type_ptr_t::type_ptr_t(type_t::ref element_type) : element_type(element_type) {
		// assert(!element_type->is_null());
	}

	std::ostream &type_ptr_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		parens_t parens(os, parent_precedence, get_precedence());
		os << "*";
		element_type->emit(os, bindings, get_precedence());
		return os;
	}

	int type_ptr_t::ftv_count() const {
		return element_type->ftv_count();
	}

	std::set<std::string> type_ptr_t::get_ftvs() const {
		return element_type->get_ftvs();
	}

	type_t::ref type_ptr_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0 && !bottom_out_free_vars) {
			return shared_from_this();
		}

		return ::type_ptr(element_type->rebind(bindings, bottom_out_free_vars));
	}

	location_t type_ptr_t::get_location() const {
		return element_type->get_location();
	}

	type_t::ref type_ptr_t::boolean_refinement(bool elimination_value, env_t::ref env) const {
		if (elimination_value) {
			/* we can eliminate truthy types, so this pointer must be just null */
			return type_null();
		}
		return shared_from_this();
	}

	type_ref_t::type_ref_t(type_t::ref element_type) : element_type(element_type) {
	}

	std::ostream &type_ref_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		parens_t parens(os, parent_precedence, get_precedence());
		os << "&";
		element_type->emit(os, bindings, get_precedence());
		return os;
	}

	int type_ref_t::ftv_count() const {
		return element_type->ftv_count();
	}

	std::set<std::string> type_ref_t::get_ftvs() const {
		return element_type->get_ftvs();
	}

	type_t::ref type_ref_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0 && !bottom_out_free_vars) {
			return shared_from_this();
		}

		return ::type_ref(element_type->rebind(bindings, bottom_out_free_vars));
	}

	location_t type_ref_t::get_location() const {
		return element_type->get_location();
	}

	type_lambda_t::type_lambda_t(identifier::ref binding, type_t::ref body) :
		binding(binding), body(body)
	{
	}

	std::ostream &type_lambda_t::emit(std::ostream &os, const map &bindings_, int parent_precedence) const {
		parens_t parens(os, parent_precedence, get_precedence());

		auto var_name = binding->get_name();
		auto new_name = gensym();
		os << "lambda " << new_name->get_name() << " ";
		map bindings = bindings_;
		bindings[var_name] = type_id(new_name);
		body->emit(os, bindings, get_precedence());
		return os;
	}

	int type_lambda_t::ftv_count() const {
		/* pretend this is getting applied */
		map bindings;
		bindings[binding->get_name()] = type_bottom();
		return body->rebind(bindings)->ftv_count();
	}

	std::set<std::string> type_lambda_t::get_ftvs() const {
		map bindings;
		bindings[binding->get_name()] = type_bottom();
		return body->rebind(bindings)->get_ftvs();
	}

	type_t::ref type_lambda_t::rebind(const map &bindings_, bool bottom_out_free_vars) const {
		if (bindings_.size() == 0) {
			return shared_from_this();
		}

		map bindings = bindings_;
		auto binding_iter = bindings.find(binding->get_name());
		if (binding_iter != bindings.end()) {
			bindings.erase(binding_iter);
		}
		return ::type_lambda(binding, body->rebind(bindings, bottom_out_free_vars));
	}

	location_t type_lambda_t::get_location() const {
		return binding->get_location();
	}

	type_integer_t::type_integer_t(type_t::ref bit_size, type_t::ref signed_) :
		bit_size(bit_size), signed_(signed_)
	{
	}

	std::ostream &type_integer_t::emit(std::ostream &os, const map &bindings_, int parent_precedence) const {
		std::stringstream ss;
		bit_size->emit(ss, bindings_, 0);
		auto bit_size_str = ss.str();
		ss.str("");
		signed_->emit(ss, bindings_, 0);
		auto signed_str = ss.str();

		bool _signed = (signed_str == "true");
		bool _unsigned = !_signed && (signed_str == "false");

		if (_signed) {
			if (bit_size_str == "64") {
				return os << "int";
			} else if (bit_size_str == "32") {
				return os << "int32";
			} else if (bit_size_str == "16") {
				return os << "int16";
			} else if (bit_size_str == "8") {
				return os << "int8";
			}
		} else if (_unsigned) {
			if (bit_size_str == "64") {
				return os << "uint";
			} else if (bit_size_str == "32") {
				return os << "uint32";
			} else if (bit_size_str == "16") {
				return os << "uint16";
			} else if (bit_size_str == "8") {
				return os << "uint8";
			}
		}

		return os << K(integer) << "(" << bit_size_str << ", " << signed_str << ")";
	}

	type_t::ref type_integer_t::boolean_refinement(bool elimination_value, env_t::ref env) const {
		return shared_from_this();
	}

	int type_integer_t::ftv_count() const {
		/* pretend this is getting applied */
		return bit_size->ftv_count() + signed_->ftv_count();
	}

	std::set<std::string> type_integer_t::get_ftvs() const {
		std::set<std::string> ftvs = bit_size->get_ftvs();
		std::set<std::string> ftvs_signed = signed_->get_ftvs();
		ftvs.insert(ftvs_signed.begin(), ftvs_signed.end());
		return ftvs;
	}

	type_t::ref type_integer_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		auto bit_size_rebound = bit_size->rebind(bindings, bottom_out_free_vars);
		auto signed_rebound = signed_->rebind(bindings, bottom_out_free_vars);
		if (bit_size_rebound != bit_size || signed_rebound != signed_) {
			return ::type_integer(bit_size_rebound, signed_rebound);
		} else {
			return shared_from_this();
		}
	}

	location_t type_integer_t::get_location() const {
		return bit_size->get_location();
	}

	type_literal_t::type_literal_t(token_t token) : token(token)
	{
	}

	std::ostream &type_literal_t::emit(std::ostream &os, const map &bindings_, int parent_precedence) const {
		return os << token.text;
	}

	int type_literal_t::ftv_count() const {
		return 0;
	}

	std::set<std::string> type_literal_t::get_ftvs() const {
		return {};
	}

	type_t::ref type_literal_t::rebind(const map &bindings_, bool bottom_out_free_vars) const {
		return shared_from_this();
	}

	location_t type_literal_t::get_location() const {
		return token.location;
	}

	int type_literal_t::coerce_to_int() const {
		std::string text = token.text;
		if (token.tk == tk_string) {
			text = unescape_json_quotes(text);
		}
		std::istringstream iss(text);
		int value;
		iss >> value;
		if (iss.fail() || !iss.eof()) {
			throw user_error(get_location(), "could not parse number from %s",
					text.c_str());
			return 0;
		}
		return value;
	}

	type_extern_t::type_extern_t(types::type_t::ref inner) : inner(inner)
	{
	}

	std::ostream &type_extern_t::emit(std::ostream &os, const map &bindings_, int parent_precedence) const {
		parens_t parens(os, parent_precedence, get_precedence());

		os << "extern ";
		inner->emit(os, bindings_, get_precedence());
		return os;
	}

	int type_extern_t::ftv_count() const {
		/* pretend this is getting applied */
		return inner->ftv_count();
	}

	std::set<std::string> type_extern_t::get_ftvs() const {
		return inner->get_ftvs();
	}

	type_t::ref type_extern_t::rebind(const map &bindings_, bool bottom_out_free_vars) const {
		if (bindings_.size() == 0) {
			return shared_from_this();
		}

		return ::type_extern(inner->rebind(bindings_, bottom_out_free_vars));
	}

	location_t type_extern_t::get_location() const {
		return inner->get_location();
	}

	type_data_t::type_data_t(
			token_t name,
			type_variable_t::refs type_vars,
			std::vector<std::pair<token_t, types::type_args_t::ref>> ctor_pairs) :
		name(name),
		type_vars(type_vars),
		ctor_pairs(ctor_pairs)
	{
	}

	std::ostream &type_data_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		if (name.text == "Maybe") {
			assert(type_vars.size() == 1);
			type_vars[0]->emit(os, bindings, 8);
			return os << "?";
		}

		parens_t parens(os, parent_precedence, get_precedence());
		os << name.text;
		for (auto type_var : type_vars) {
			os << " ";
			type_var->emit(os, bindings, get_precedence());
		}
	   	os << " " << K(is);
		for (auto ctor_pair : ctor_pairs) {
			os << " ";
			os << ctor_pair.first.text;
			if (ctor_pair.second->args.size() != 0) {
				ctor_pair.second->emit(os, bindings, get_precedence());
			}
		}
		return os;
	}

	void type_data_t::encode(env_t::ref env, std::vector<uint16_t> &encoding) const {
		assert(false);
	}

	int type_data_t::ftv_count() const {
		int ftv_sum = 0;
		for (auto type_var : type_vars) {
			ftv_sum += type_var->ftv_count();
		}
		for (auto ctor_pair : ctor_pairs) {
			ftv_sum += ctor_pair.second->ftv_count();
		}
		return ftv_sum;
	}

	std::set<std::string> type_data_t::get_ftvs() const {
		std::set<std::string> set;
		for (auto type_var : type_vars) {
			std::set<std::string> option_set = type_var->get_ftvs();
			set.insert(option_set.begin(), option_set.end());
		}
		for (auto ctor_pair : ctor_pairs) {
			std::set<std::string> option_set = ctor_pair.second->get_ftvs();
			set.insert(option_set.begin(), option_set.end());
		}
		return set;
	}

	type_t::ref type_data_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() == 0 && !bottom_out_free_vars) {
			return shared_from_this();
		}

		bool found_new = false;
		type_variable_t::refs new_type_vars;
		new_type_vars.reserve(type_vars.size());
		for (auto type_var : type_vars) {
			new_type_vars.push_back(type_var->rebind(bindings, bottom_out_free_vars));
			if (new_type_vars.back() != type_var) {
				found_new = true;
			}
		}

		std::vector<std::pair<token_t, type_args_t::ref>> new_ctor_pairs;
		for (auto ctor_pair : ctor_pairs) {
			types::type_args_t::ref elem = dyncast<const type_args_t>(ctor_pair.second->rebind(bindings, bottom_out_free_vars));
			assert(elem != nullptr);
			new_ctor_pairs.push_back({ctor_pair.first, elem});
			if (elem != ctor_pair.second) {
				found_new = true;
			}
		}
		if (found_new) {
			return ::type_data(name, new_type_vars, new_ctor_pairs);
		} else {
			return shared_from_this();
		}
	}

	location_t type_data_t::get_location() const {
		return name.location;
	}

	type_t::ref type_data_t::boolean_refinement(bool elimination_value, env_t::ref env) const {
		return shared_from_this();
	}

	bool is_ptr_type_id(
			type_t::ref type,
			const std::string &type_name,
			env_t::ref _env,
			bool allow_maybe)
	{
		auto env = (_env == nullptr) ? _empty_env : _env;

		type = type->eval(env, false /*get_structural_type*/);

		if (allow_maybe) {
			if (auto maybe = dyncast<const types::type_maybe_t>(type)) {
				type = maybe->just;
			}
		}
		if (auto ptr_type = dyncast<const types::type_ptr_t>(type)) {
			return is_type_id(ptr_type->element_type, type_name, env);
		}
		return false;
	}


	struct empty_env : public env_t {
		empty_env() {}

		virtual ~empty_env() {}
		virtual types::type_t::ref get_type(const std::string &name, bool allow_structural_types) const {
			return nullptr;
		}
	};

	env_t::ref _empty_env = make_ptr<empty_env>();

	bool is_type_id(type_t::ref type, const std::string &type_name, env_t::ref _env) {
		env_t::ref env = (_env == nullptr) ? _empty_env : _env;
		type = type->eval(env);

		if (auto pti = dyncast<const types::type_id_t>(type)) {
			return pti->id->get_name() == type_name;
		}

		return false;
	}

	bool is_managed_ptr(types::type_t::ref type, env_t::ref _env) {
		env_t::ref env = (_env == nullptr) ? _empty_env : _env;
		debug_above(9, log(log_info, "checking if %s is a managed ptr", type->str().c_str()));
		if (auto expanded_type = type->eval(env, true /*get_structural_type*/)) {
			type = expanded_type;
		}

		if (auto ref_type = dyncast<const types::type_ref_t>(type)) {
			type = ref_type->element_type;
		}

		if (auto maybe_type = dyncast<const types::type_maybe_t>(type)) {
			type = maybe_type->just;
		}

		if (auto data_type = dyncast<const types::type_data_t>(type)) {
			return true;
		}

		if (auto tuple_type = dyncast<const types::type_tuple_t>(type)) {
			return true;
		}

		if (auto ptr_type = dyncast<const types::type_ptr_t>(type)) {
			if (dyncast<const types::type_managed_t>(ptr_type->element_type)) {
				return true;
			}
		}

		if (auto extern_type = dyncast<const types::type_extern_t>(type)) {
			return true;
		}

		return false;
	}

	bool is_ptr(types::type_t::ref type, env_t::ref env) {
		// REVIEW: this is nebulous, it really depends on what env is passed in
		type = type->eval(env, true /*get_structural_type*/);

		if (auto maybe_type = dyncast<const types::type_maybe_t>(type)) {
			type = maybe_type->just;
		}

		if (auto ptr_type = dyncast<const types::type_ptr_t>(type)) {
			return true;
		}

		if (auto extern_type = dyncast<const types::type_extern_t>(type)) {
			/* extern types are always managed pointers for now */
			return true;
		}

		return false;
	}

	int coerce_to_integer(
			env_t::ref env,
			type_t::ref type,
			type_t::ref &expansion)
	{
		expansion = type->eval(env);

		if (auto literal = dyncast<const type_literal_t>(expansion)) {
			return literal->coerce_to_int();
		} else {
			throw user_error(type->get_location(),
					"unable to deduce an integer value from type %s",
					expansion->str().c_str());
		}
	}

	bool is_integer(type_t::ref type, env_t::ref _env) {
		auto env = (_env == nullptr) ? _empty_env : _env;
		auto expansion = type->eval(env);
		return dyncast<const type_integer_t>(expansion) != nullptr;
	}

	void get_integer_attributes(
			location_t location,
			type_t::ref type,
			env_t::ref env,
			unsigned &bit_size,
			bool &signed_)
	{
		type = type->eval(env);
		if (auto integer = dyncast<const type_integer_t>(type)) {
			type_t::ref bit_size_expansion;
			bit_size = coerce_to_integer(env, integer->bit_size, bit_size_expansion);
			auto signed_type = integer->signed_->eval(env);
			if (types::is_type_id(signed_type, TRUE_TYPE, nullptr)) {
				signed_ = true;
				return;
			} else if (types::is_type_id(signed_type, FALSE_TYPE, nullptr)) {
				signed_ = false;
				return;
			} else {
				throw user_error(integer->signed_->get_location(), "unable to determine signedness for type from %s",
						signed_type->str().c_str());
			}
		} else if (types::is_type_id(type, CHAR_TYPE, nullptr)) {
			bit_size = 8;
			signed_ = false;
			return;
		} else {
			throw user_error(location, "expected an integer type, found %s", type->str().c_str());
		}
	}

	void get_runtime_typeids(type_t::ref type, env_t::ref env, std::set<int> &typeids) {
		auto expansion = type->eval(env);
		debug_above(7, log("get_runtime_typeids expanded to %s", expansion->str().c_str()));
		if (auto type_ref = dyncast<const type_ref_t>(expansion)) {
			throw user_error(type->get_location(), "reference types are not allowed here. %s does not have runtime type information",
					type->str().c_str());
		} else if (auto type_ptr = dyncast<const type_ptr_t>(expansion)) {
			throw user_error(type->get_location(), "pointer types are not allowed here. %s does not have runtime type information",
					type->str().c_str());
		} else if (auto type_id = dyncast<const type_id_t>(expansion)) {
			typeids.insert(atomize(type_id->repr()));
		} else if (auto type_operator = dyncast<const type_operator_t>(expansion)) {
			typeids.insert(atomize(type_operator->repr()));
		} else {
			assert(false);
		}
	}

	type_t::ref without_ref(type_t::ref type) {
		if (auto ref = dyncast<const type_ref_t>(type)) {
			return ref->element_type;
		} else {
			return type;
		}
	}

	type_t::refs without_refs(type_t::refs types) {
		type_t::refs dims;
		dims.reserve(types.size());
		for (auto type : types) {
			dims.push_back(without_ref(type));
		}
		return dims;
	}

	type_function_t::ref without_closure(type_t::ref type) {
		auto function_closure = dyncast<const types::type_function_closure_t>(type);
		if (function_closure != nullptr) {
			return dyncast<const types::type_function_t>(function_closure->function);
		} else {
			return dyncast<const types::type_function_t>(type);
		}
	}

	types::type_t::ref freshen(types::type_t::ref type) {
		if (type == nullptr) {
			return type;
		}
		auto ftvs = type->get_ftvs();
		if (ftvs.size() != 0) {
			type_t::map bindings;
			for (auto ftv : ftvs) {
				bindings[ftv] = type_variable(INTERNAL_LOC());
			}
			return type->rebind(bindings);
		} else {
			return type;
		}
	}

	bool share_ftvs(type_t::ref lhs, type_t::ref rhs) {
		std::set<std::string> shared_ftvs;
		auto lhs_ftvs = lhs->get_ftvs();
		auto rhs_ftvs = rhs->get_ftvs();
		std::set_intersection(
				lhs_ftvs.begin(), lhs_ftvs.end(),
				rhs_ftvs.begin(), rhs_ftvs.end(),
				std::insert_iterator<std::set<std::string>>(shared_ftvs, shared_ftvs.begin()));

		return shared_ftvs.size() != 0;
	}
}

types::type_t::ref type_id(identifier::ref id) {
	if (id->get_name().find("std.") == 0) {
		dbg();
	}
	return make_ptr<types::type_id_t>(id);
}

types::type_t::ref type_variable(identifier::ref id) {
	return make_ptr<types::type_variable_t>(id);
}

types::type_t::ref type_variable(location_t location) {
	return make_ptr<types::type_variable_t>(location);
}

types::type_t::ref type_unit() {
    return type_tuple({});
}

types::type_t::ref type_bottom() {
	static auto bottom_type = make_ptr<types::type_id_t>(make_iid(BOTTOM_TYPE));
	return bottom_type;
}

types::type_t::ref type_null() {
	static auto null_type = make_ptr<types::type_id_t>(make_iid(NULL_TYPE));
	return null_type;
}

types::type_t::ref type_void() {
	return make_ptr<types::type_id_t>(make_iid(VOID_TYPE));
}

types::type_t::ref type_operator(types::type_t::ref operator_, types::type_t::ref operand) {
	return make_ptr<types::type_operator_t>(operator_, operand);
}

types::type_t::ref type_subtype(types::type_t::ref lhs, types::type_t::ref rhs) {
	return make_ptr<types::type_subtype_t>(lhs, rhs);
}

types::name_index_t get_name_index_from_ids(identifier::refs ids) {
	types::name_index_t name_index;
	int i = 0;
	for (auto id : ids) {
		name_index[id->get_name()] = i++;
	}
	return name_index;
}

types::type_struct_t::ref type_struct(types::type_args_t::ref type_args) {
	return ::type_struct(
			type_args->args,
			get_name_index_from_ids(type_args->names));
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

types::type_tuple_t::ref type_tuple(types::type_t::refs dimensions) {
	return make_ptr<types::type_tuple_t>(dimensions);
}

types::type_args_t::ref type_args(
	   	types::type_t::refs args,
		const identifier::refs &names)
{
	assert((names.size() == args.size()) ^ (names.size() == 0 && args.size() != 0));
	for (auto arg : args) {
		assert(dyncast<const types::type_ref_t>(arg) == nullptr);
	}
	return make_ptr<types::type_args_t>(args, names);
}

types::type_injection_t::ref type_injection(types::type_t::ref module_type) {
	return make_ptr<types::type_injection_t>(module_type);
}

types::type_managed_t::ref type_managed(types::type_t::ref element_type) {
	return make_ptr<types::type_managed_t>(element_type);
}

types::type_function_t::ref type_function(
		location_t location,
		types::type_t::ref type_constraints,
		types::type_t::ref args,
		types::type_t::ref return_type)
{
	auto ret = make_ptr<types::type_function_t>(location, type_constraints, args, return_type);
	if (type_constraints && type_constraints->repr() == TRUE_TYPE) {
		debug_above(9, log("created type_function %s", ret->str().c_str()));
		dbg();
		types::is_type_id(type_constraints, TRUE_TYPE, nullptr);
	}
	return ret;
}

types::type_function_closure_t::ref type_function_closure(types::type_t::ref type_function) {
	return make_ptr<types::type_function_closure_t>(type_function);
}

bool types_contains(const types::type_t::refs &options, std::string signature) {
	for (auto &option : options) {
		if (option->get_signature() == signature) {
			return true;
		}
	}
	return false;
}

types::type_t::ref type_and(types::type_t::refs terms) {
	return make_ptr<types::type_and_t>(terms);
}

types::type_t::ref type_eq(types::type_t::ref lhs, types::type_t::ref rhs, location_t location) {
	return make_ptr<types::type_eq_t>(lhs, rhs, location);
}

types::type_t::ref type_literal(token_t token) {
	assert(token.tk == tk_integer || token.tk == tk_string || token.tk == tk_identifier);
	return make_ptr<types::type_literal_t>(token);
}

types::type_t::ref type_integer(types::type_t::ref bit_size, types::type_t::ref signed_) {
	return make_ptr<types::type_integer_t>(bit_size, signed_);
}

types::type_t::ref type_maybe(types::type_t::ref just, env_t::ref env) {
	if (dyncast<const types::type_ptr_t>(just) == nullptr) {
		types::type_t::ref expanded_just =
			(env != nullptr)
			? just->eval(env, true /*get_structural_type*/)
			: nullptr;

		if (just->eval_predicate(tb_null, env)) {
			/* maybe of null is just null */
			return just;
		}

		if (dyncast<const types::type_ptr_t>(expanded_just) == nullptr) {
			throw user_error(just->get_location(), "type %s cannot be a maybe type since it is not a pointer",
					just->str().c_str());
		}
	}

    if (auto maybe = dyncast<const types::type_maybe_t>(just)) {
        return just;
    }

    return make_ptr<types::type_maybe_t>(just);
}

types::type_ptr_t::ref type_ptr(types::type_t::ref raw) {
    return make_ptr<types::type_ptr_t>(raw);
}

types::type_t::ref type_ref(types::type_t::ref raw) {
    assert(!dyncast<const types::type_ref_t>(raw));
    return make_ptr<types::type_ref_t>(raw);
}

types::type_t::ref type_lambda(identifier::ref binding, types::type_t::ref body) {
    return make_ptr<types::type_lambda_t>(binding, body);
}

types::type_t::ref type_extern(types::type_t::ref inner)
{
    return make_ptr<types::type_extern_t>(inner);
}

types::type_t::ref type_data(
		token_t name,
	   	types::type_variable_t::refs type_vars,
	   	std::vector<std::pair<token_t, types::type_args_t::ref>> ctor_pairs)
{
	return make_ptr<types::type_data_t>(name, type_vars, ctor_pairs);
}

types::type_t::ref type_list_type(types::type_t::ref element) {
	return type_maybe(type_operator(type_id(make_iid_impl(
						STD_VECTOR_TYPE, element->get_location())), element), {});
}

types::type_t::ref type_vector_type(types::type_t::ref element) {
	return type_operator(type_id(make_iid_impl(
					STD_VECTOR_TYPE, element->get_location())), element);
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

	auto module_id = make_iid(GLOBAL_SCOPE_NAME);
	return types::type_t::pair{
		parse_type_expr(fst, generics, module_id),
	   	parse_type_expr(snd, generics, module_id)};
}

bool get_type_variable_name(types::type_t::ref type, std::string &name) {
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

std::string str(const types::type_t::map &coll) {
	std::stringstream ss;
	ss << "{";
	const char *sep = "";
	std::vector<std::string> symbols = keys(coll);
	std::sort(symbols.begin(), symbols.end());
	for (auto symbol : symbols) {
		ss << sep << C_ID << symbol << C_RESET ": ";
		ss << coll.find(symbol)->second->str().c_str();
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
	case pk_tuple:
		return "tuple";
	case pk_managed:
		return "managed";
	case pk_args:
		return "args";
	}
	assert(false);
	return nullptr;
}

std::ostream &join_dimensions(std::ostream &os, const types::type_t::refs &dimensions, const types::name_index_t &name_index, const types::type_t::map &bindings) {
	const char *sep = "";
	int i = 0;
	for (auto dimension : dimensions) {
		os << sep;
		auto name = get_name_from_index(name_index, i++);
		if (name.size() != 0) {
			os << name << " ";
		}
		dimension->emit(os, bindings, 0);
		sep = ", ";
	}
	return os;
}

bool is_valid_udt_initial_char(int ch) {
	return ch == '_' || isupper(ch);
}

types::type_t::ref get_arg_from_function(types::type_function_t::ref function, int i) {
	if (function != nullptr) {
		if (auto args = dyncast<const types::type_args_t>(function->args)) {
			if (args->args.size() <= i) {
				throw user_error(function->get_location(), "invalid indexed access (%d) to arguments list for function %s",
						i, function->str().c_str());
			}
			return args->args[i];
		} else {
			assert(false);
			return nullptr;
		}
	} else {
		return nullptr;
	}
}

