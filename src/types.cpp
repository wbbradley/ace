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

const char *BUILTIN_NULL_TYPE = "null";
const char *STD_MANAGED_TYPE = "var_t";
const char *STD_VECTOR_TYPE = "vector.Vector";
const char *STD_MAP_TYPE = "map.Map";
const char *BUILTIN_VOID_TYPE = "void";
const char *BUILTIN_UNREACHABLE_TYPE = "__unreachable";

const char *TYPE_OP_NOT = "not";
const char *TYPE_OP_IF = "if";
const char *TYPE_OP_GC = "gc";

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

	std::string type_t::repr(const map &bindings) const {
		std::stringstream ss;
		emit(ss, bindings);
		return ss.str();
	}

	type_t::ref type_t::boolean_refinement(
			status_t &status,
			bool elimination_value,
			const types::type_t::map &nominal_env,
			const types::type_t::map &total_env) const
	{
		return shared_from_this();
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

	std::set<std::string> type_id_t::get_ftvs() const {
		return {};
	}

	type_t::ref type_id_t::rebind(const map &bindings) const {
		return shared_from_this();
	}

	location_t type_id_t::get_location() const {
		return id->get_location();
	}

	type_t::ref type_id_t::boolean_refinement(
			status_t &status,
			bool elimination_value,
			const types::type_t::map &nominal_env,
			const types::type_t::map &total_env) const
	{
		debug_above(6, log("refining %s. looking to eliminate %s values from the type", str().c_str(), boolstr(elimination_value)));
		auto evaled = eval_core(nominal_env, total_env, false /*get_structural_type*/);
		if (auto id_type = dyncast<const type_id_t>(evaled)) {
			auto name = id_type->id->get_name();
			if (name == BUILTIN_NULL_TYPE || name == ZERO_TYPE) {
				if (elimination_value) {
					debug_above(6, log("keeping %s", str().c_str()));
					return shared_from_this();
				} else {
					debug_above(6, log("eliding the whole type of %s", str().c_str()));
					return nullptr;
				}
			}

			/* handle builtin type ids */
			if (name == boolstr(elimination_value) || name == Boolstr(elimination_value)) {
				debug_above(6, log("eliding the whole type of %s", str().c_str()));
				return nullptr;
			} else if (name == BOOL_TYPE) {
				auto refinement = type_id(make_iid_impl(boolstr(!elimination_value), get_location()));
				debug_above(6, log("refining %s to %s", str().c_str(), refinement->str().c_str()));
				return refinement;
			} else if (name == MANAGED_BOOL) {
				auto refinement = type_id(make_iid_impl(Boolstr(!elimination_value), get_location()));
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

	std::ostream &type_variable_t::emit(std::ostream &os, const map &bindings) const {
		auto instance_iter = bindings.find(id->get_name());
		if (instance_iter != bindings.end()) {
			assert(instance_iter->second != shared_from_this());
			return instance_iter->second->emit(os, bindings);
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

	type_operator_t::type_operator_t(type_t::ref oper, type_t::ref operand) :
		oper(oper), operand(operand)
	{
	}

	std::ostream &type_operator_t::emit(std::ostream &os, const map &bindings) const {
		if (is_type_id(oper->rebind(bindings), STD_VECTOR_TYPE, {}, {})) {
			os << "[";
			operand->emit(os, bindings);
			return os << "]";
		} else {
			// TODO: detect map.Map X Y which is (oper (oper map.Map X) Y)
			bool operator_needs_parens = oper->get_precedence() < get_precedence();

			if (operator_needs_parens) {
				os << "(";
			}
			oper->emit(os, bindings);
			if (operator_needs_parens) {
				os << ")";
			}

			os << " ";

			bool operand_needs_parens = operand->get_precedence() <= get_precedence();
			if (operand_needs_parens) {
				os << "(";
			}
			operand->emit(os, bindings);
			if (operand_needs_parens) {
				os << ")";
			}
			return os;
		}
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

	type_t::ref type_operator_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		return ::type_operator(oper->rebind(bindings), operand->rebind(bindings));
	}

	location_t type_operator_t::get_location() const {
		return oper->get_location();
	}

	type_t::ref type_operator_t::boolean_refinement(
			status_t &status,
			bool elimination_value,
			const types::type_t::map &nominal_env,
			const types::type_t::map &total_env) const
	{
		auto expansion = eval(nominal_env, total_env);
		if (expansion != nullptr) {
			/* refine the expanded version of this type */
			auto refined_expansion = expansion->boolean_refinement(status, elimination_value, nominal_env, total_env);
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

	std::ostream &type_struct_t::emit(std::ostream &os, const map &bindings) const {
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


	type_t::ref type_struct_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		bool anything_was_rebound = false;
		refs type_dimensions;
		for (auto dimension : dimensions) {
			auto new_dim = dimension->rebind(bindings);
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

	std::ostream &type_tuple_t::emit(std::ostream &os, const map &bindings) const {
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


	type_t::ref type_tuple_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		bool anything_was_rebound = false;
		refs type_dimensions;
		for (auto dimension : dimensions) {
			auto new_dim = dimension->rebind(bindings);
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

	std::ostream &type_args_t::emit(std::ostream &os, const map &bindings) const {
		os << "(";
		const char *sep = "";
		int i = 0;
		for (auto arg : args) {
			os << sep;
			if (names.size() != 0) {
				auto name = names[i++]->get_name();
				if (name.size() != 0) {
					os << name << " ";
				}
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

	std::set<std::string> type_args_t::get_ftvs() const {
		std::set<std::string> set;
		for (auto arg : args) {
			std::set<std::string> dim_set = arg->get_ftvs();
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


	std::ostream &type_managed_t::emit(std::ostream &os, const map &bindings) const {
		os << "managed{";
		element_type->emit(os, bindings);
		os << "}";
		return os;
	}

	int type_managed_t::ftv_count() const {
		return element_type->ftv_count();
	}

	std::set<std::string> type_managed_t::get_ftvs() const {
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

	std::ostream &type_module_t::emit(std::ostream &os, const map &bindings) const {
		os << "module ";
		module_type->emit(os, bindings);
		return os;
	}

	int type_module_t::ftv_count() const {
		return module_type->ftv_count();
	}

	std::set<std::string> type_module_t::get_ftvs() const {
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

	type_function_t::type_function_t(
			identifier::ref name,
			types::type_t::ref type_constraints,
			types::type_t::ref args,
			type_t::ref return_type) :
		name(name), type_constraints(type_constraints),
		args(args), return_type(return_type)
	{
		assert(dyncast<const type_args_t>(args) != nullptr || dyncast<const type_variable_t>(args) != nullptr);
		assert(return_type != nullptr);
	}

	std::ostream &type_function_t::emit(std::ostream &os, const map &bindings) const {
		os << K(def) << " ";
		if (name != nullptr) {
			os << C_ID << name->get_name() << C_RESET;
		}
		if (type_constraints != nullptr && !is_type_id(type_constraints->rebind(bindings), TRUE_TYPE, {}, {})) {
			os << "[" << C_CONTROL << "where " << C_RESET;
			type_constraints->emit(os, bindings);
			os << "]";
		}
		args->emit(os, bindings);
		os << " ";
		return return_type->emit(os, bindings);
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

	type_t::ref type_function_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		types::type_args_t::ref rebound_args = dyncast<const types::type_args_t>(
				args->rebind(bindings));
		assert(args != nullptr);
		return ::type_function(
				name,
				type_constraints != nullptr ? type_constraints->rebind(bindings) : type_constraints,
				rebound_args,
				return_type->rebind(bindings));
	}

	location_t type_function_t::get_location() const {
		return args->get_location();
	}

	type_lazy_t::type_lazy_t(const type_t::refs &options, location_t location) : options(options), location(location) {
	}

	std::ostream &type_lazy_t::emit(std::ostream &os, const map &bindings) const {
		const char *delim = "";
		assert(options.size() != 0);
		os << "(lazy ";
		for (auto option : options) {
			os << delim;
			option->emit(os, bindings);
			delim = " or ";
		}
		return os << ")";
	}

	int type_lazy_t::ftv_count() const {
		int ftv_sum = 0;
		for (auto option : options) {
			ftv_sum += option->ftv_count();
		}
		return ftv_sum;
	}

	std::set<std::string> type_lazy_t::get_ftvs() const {
		std::set<std::string> set;
		for (auto option : options) {
			std::set<std::string> option_set = option->get_ftvs();
			set.insert(option_set.begin(), option_set.end());
		}
		return set;
	}

	type_t::ref type_lazy_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		refs type_options;
		for (auto option : options) {
			type_options.push_back(option->rebind(bindings));
		}
		return ::type_lazy(type_options, location);
	}

	location_t type_lazy_t::get_location() const {
		return location;
	}

	type_t::ref type_lazy_t::boolean_refinement(
			status_t &status,
			bool elimination_value,
			const types::type_t::map &nominal_env,
			const types::type_t::map &total_env) const
	{
		auto type = type_sum_safe(
				status,
				options,
				location,
				nominal_env,
				total_env);
		if (!!status) {
			return type->boolean_refinement(status, elimination_value,
					nominal_env, total_env);
		}
		assert(!status);
		return nullptr;
	}

	type_sum_t::type_sum_t(type_t::refs options, location_t location) : options(options), location(location) {
		for (auto option : options) {
			if (dyncast<const type_maybe_t>(option) != nullptr) {
				log(log_error, "found maybe type %s within type_sum %s", option->str().c_str(), ::str(options).c_str());
				dbg();
			}
			if (is_type_id(option, BUILTIN_NULL_TYPE, {}, {})) {
				log(log_error, "found null type within type_sum %s", ::str(options).c_str());
				dbg();
			}
		}
	}

	std::ostream &type_sum_t::emit(std::ostream &os, const map &bindings) const {
		const char *delim = "";
		assert(options.size() != 0);
		for (auto option : options) {
			os << delim;
			option->emit(os, bindings);
			delim = " or ";
		}
		return os;
	}

	int type_sum_t::ftv_count() const {
		int ftv_sum = 0;
		for (auto option : options) {
			ftv_sum += option->ftv_count();
		}
		return ftv_sum;
	}

	std::set<std::string> type_sum_t::get_ftvs() const {
		std::set<std::string> set;
		for (auto option : options) {
			std::set<std::string> option_set = option->get_ftvs();
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
		return ::type_sum(type_options, location);
	}

	location_t type_sum_t::get_location() const {
		return location;
	}

	type_t::ref type_sum_t::boolean_refinement(
			status_t &status,
			bool elimination_value,
			const types::type_t::map &nominal_env,
			const types::type_t::map &total_env) const
	{
		types::type_t::refs new_options;
		for (auto option : options) {
			option = option->boolean_refinement(status, elimination_value, nominal_env, total_env);
			if (!status) {
				break;
			}
			if (option != nullptr) {
				new_options.push_back(option);
			}
		}

		if (!!status) {
			return type_sum_safe(status, new_options, get_location(), nominal_env, total_env);
		}
		assert(!status);
		return nullptr;
	}

	type_and_t::type_and_t(type_t::refs terms) : terms(terms) {
		for (auto term : terms) {
			assert(!dyncast<const type_maybe_t>(term));
		}
	}

	std::ostream &type_and_t::emit(std::ostream &os, const map &bindings) const {
		const char *delim = "";
		assert(terms.size() != 0);
		for (auto term : terms) {
			os << delim;
			term->emit(os, bindings);
			delim = " or ";
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

	type_t::ref type_and_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		refs type_options;
		for (auto term : terms) {
			type_options.push_back(term->rebind(bindings));
		}
		return ::type_sum(type_options, location);
	}

	location_t type_and_t::get_location() const {
		return location;
	}

	type_maybe_t::type_maybe_t(type_t::ref just) : just(just) {
		assert(!dyncast<const type_maybe_t>(just));
		assert(!dyncast<const type_ref_t>(just));
		// TODO: revisit this... lazy types...
		// assert(!just->is_null());
	}

	std::ostream &type_maybe_t::emit(std::ostream &os, const map &bindings) const {
		if (auto pointer = dyncast<const type_ptr_t>(just)) {
			/* this is a native pointer that might be null */
			os << "*?";
			return pointer->element_type->emit(os, bindings);
		} else {
			/* this is a managed pointer that might be null. we subsume the maybeness onto the whole typename in order
			 * to look nicer. */
			auto element = just->rebind(bindings);
			bool needs_parens = element->get_precedence() < get_precedence();
			if (needs_parens) {
				os << "(";
				element->emit(os, {});
				os << ")";
			} else {
				element->emit(os, {});
			}
			return os << "?";
		}
	}

	int type_maybe_t::ftv_count() const {
		return just->ftv_count();
	}

	std::set<std::string> type_maybe_t::get_ftvs() const {
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

	type_t::ref type_maybe_t::boolean_refinement(
			status_t &status,
			bool elimination_value,
			const types::type_t::map &nominal_env,
			const types::type_t::map &total_env) const
	{
		auto just_refined = just->boolean_refinement(status, elimination_value, nominal_env, total_env);
		if (!elimination_value) {
			/* we are eliminating falseyness, so we can eliminate the maybeness, too */
			return just_refined;
		} else {
			if (just_refined != just) {
				/* eliminate truthyness. the just refinement returned a new object, so let's construct a maybe around
				 * it, since we can not eliminate the maybe when we are eliminating truthyness */
				return ::type_maybe(just_refined);
			} else {
				/* nothing learned from this refinement */
				return shared_from_this();
			}
		}
	}

	type_ptr_t::type_ptr_t(type_t::ref element_type) : element_type(element_type) {
		// assert(!element_type->is_null());
	}

	std::ostream &type_ptr_t::emit(std::ostream &os, const map &bindings) const {
		os << "*";
		auto element = element_type->rebind(bindings);
		bool element_needs_parens = element->get_precedence() < get_precedence();
		if (element_needs_parens) {
			os << "(";
			element->emit(os, {});
			os << ")";
		} else {
			element->emit(os, {});
		}
		return os;
	}

	int type_ptr_t::ftv_count() const {
		return element_type->ftv_count();
	}

	std::set<std::string> type_ptr_t::get_ftvs() const {
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

	type_t::ref type_ptr_t::boolean_refinement(
			status_t &status,
			bool elimination_value,
			const types::type_t::map &nominal_env,
			const types::type_t::map &total_env) const
	{
		if (elimination_value) {
			/* we can eliminate truthy types, so this pointer must be just null */
			return type_null();
		}
		return shared_from_this();
	}

	type_ref_t::type_ref_t(type_t::ref element_type) : element_type(element_type) {
	}

	std::ostream &type_ref_t::emit(std::ostream &os, const map &bindings) const {
		os << "&";
		auto element = element_type->rebind(bindings);
		bool element_needs_parens = element->get_precedence() < get_precedence();
		if (element_needs_parens) {
			os << "(";
			element->emit(os, {});
			os << ")";
		} else {
			element->emit(os, {});
		}
		return os;
	}

	int type_ref_t::ftv_count() const {
		return element_type->ftv_count();
	}

	std::set<std::string> type_ref_t::get_ftvs() const {
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

	type_lambda_t::type_lambda_t(identifier::ref binding, type_t::ref body) :
		binding(binding), body(body)
	{
	}

	std::ostream &type_lambda_t::emit(std::ostream &os, const map &bindings_) const {
		auto var_name = binding->get_name();
		auto new_name = gensym();
		os << "(lambda " << new_name->get_name() << " ";
		map bindings = bindings_;
		bindings[var_name] = type_id(new_name);
		body->emit(os, bindings);
		return os << ")";
	}

	int type_lambda_t::ftv_count() const {
		/* pretend this is getting applied */
		panic("This should not really get called ....");
		map bindings;
		bindings[binding->get_name()] = type_unreachable();
		return body->rebind(bindings)->ftv_count();
	}

	std::set<std::string> type_lambda_t::get_ftvs() const {
		panic("This should not really get called ....");
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

	type_integer_t::type_integer_t(type_t::ref bit_size, type_t::ref signed_) :
		bit_size(bit_size), signed_(signed_)
	{
	}

	std::ostream &type_integer_t::emit(std::ostream &os, const map &bindings_) const {
		os << K(integer) << "(";
		bit_size->emit(os, bindings_);
		os << ", ";
		signed_->emit(os, bindings_);
		return os << ")";
	}

	type_t::ref type_integer_t::boolean_refinement(
			status_t &status,
			bool elimination_value,
			const types::type_t::map &nominal_env,
			const types::type_t::map &total_env) const
	{
		if (elimination_value) {
			/* falsey integers are zero, let's treat them that way */
			return type_id(make_iid_impl(ZERO_TYPE, get_location()));
		} else {
			return shared_from_this();
		}
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

	type_t::ref type_integer_t::rebind(const map &bindings) const {
		auto bit_size_rebound = bit_size->rebind(bindings);
		auto signed_rebound = signed_->rebind(bindings);
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

	std::ostream &type_literal_t::emit(std::ostream &os, const map &bindings_) const {
		return os << token.text;
	}

	int type_literal_t::ftv_count() const {
		return 0;
	}

	std::set<std::string> type_literal_t::get_ftvs() const {
		return {};
	}

	type_t::ref type_literal_t::rebind(const map &bindings_) const {
		return shared_from_this();
	}

	location_t type_literal_t::get_location() const {
		return token.location;
	}

	int type_literal_t::coerce_to_int(status_t &status) const {
		std::string text = token.text;
		if (token.tk == tk_string) {
			text = unescape_json_quotes(text);
		}
		std::istringstream iss(text);
		int value;
		iss >> value;
		if (iss.fail() || !iss.eof()) {
			user_error(status, get_location(), "could not parse number from %s",
					text.c_str());
			return 0;
		}
		return value;
	}

	type_extern_t::type_extern_t(types::type_t::ref inner) : inner(inner)
	{
	}

	std::ostream &type_extern_t::emit(std::ostream &os, const map &bindings_) const {
		os << "(extern ";
		inner->emit(os, bindings_);
		return os << ")";
	}

	int type_extern_t::ftv_count() const {
		/* pretend this is getting applied */
		return inner->ftv_count();
	}

	std::set<std::string> type_extern_t::get_ftvs() const {
		return inner->get_ftvs();
	}

	type_t::ref type_extern_t::rebind(const map &bindings_) const {
		if (bindings_.size() == 0) {
			return shared_from_this();
		}

		return ::type_extern(inner->rebind(bindings_));
	}

	location_t type_extern_t::get_location() const {
		return inner->get_location();
	}

	bool is_ptr_type_id(
			type_t::ref type,
		   	const std::string &type_name,
		   	const types::type_t::map &nominal_env,
		   	const types::type_t::map &total_env,
		   	bool allow_maybe)
   	{
		type = type->eval(nominal_env, total_env, true /*get_structural_type*/);

		if (allow_maybe) {
			if (auto maybe = dyncast<const types::type_maybe_t>(type)) {
				type = maybe->just;
			}
		}
		assert(!dyncast<const type_lazy_t>(type));
		if (auto ptr_type = dyncast<const types::type_ptr_t>(type)) {
			return is_type_id(ptr_type->element_type, type_name, nominal_env, total_env);
		}
		return false;
	}

	bool is_type_id(type_t::ref type, const std::string &type_name, const types::type_t::map &nominal_env, const types::type_t::map &total_env) {
		type = type->eval(nominal_env, total_env);

		if (auto pti = dyncast<const types::type_id_t>(type)) {
			return pti->id->get_name() == type_name;
		}

		return false;
	}

	bool is_managed_ptr(types::type_t::ref type, const types::type_t::map &nominal_env, const types::type_t::map &total_env) {
		debug_above(6, log(log_info, "checking if %s is a managed ptr", type->str().c_str()));
		if (auto expanded_type = type->eval(nominal_env, total_env, true /*get_structural_type*/)) {
			type = expanded_type;
		}

		if (auto ref_type = dyncast<const types::type_ref_t>(type)) {
			type = ref_type->element_type;
		}

		if (auto maybe_type = dyncast<const types::type_maybe_t>(type)) {
			type = maybe_type->just;
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

		if (auto ptr_type = dyncast<const types::type_sum_t>(type)) {
			/* sum types are always managed pointers for now */
			return true;
		}
		return false;
	}

	bool is_ptr(types::type_t::ref type, const types::type_t::map &nominal_env, const types::type_t::map &total_env) {
		// REVIEW: this is nebulous, it really depends on what env is passed in
		type = type->eval(nominal_env, total_env, true /*get_structural_type*/);

		if (auto maybe_type = dyncast<const types::type_maybe_t>(type)) {
			type = maybe_type->just;
		}

		if (auto ptr_type = dyncast<const types::type_ptr_t>(type)) {
			return true;
		}

		if (auto ptr_type = dyncast<const types::type_sum_t>(type)) {
			/* sum types are always managed pointers for now */
			return true;
		}
		if (auto extern_type = dyncast<const types::type_extern_t>(type)) {
			/* extern types are always managed pointers for now */
			return true;
		}

		return false;
	}

	int coerce_to_integer(
			status_t &status,
			const types::type_t::map &nominal_env,
			const types::type_t::map &total_env,
			type_t::ref type,
			type_t::ref &expansion)
	{
		expansion = type->eval(nominal_env, total_env);

		if (auto literal = dyncast<const type_literal_t>(expansion)) {
			return literal->coerce_to_int(status);
		} else {
			user_error(status, type->get_location(),
					"unable to deduce an integer value from type %s",
					expansion->str().c_str());
		}
		assert(!status);
		return 0;
	}

	bool is_integer(type_t::ref type, const type_t::map &nominal_env, const type_t::map &total_env) {
		auto expansion = type->eval(nominal_env, total_env);
		return (dyncast<const type_integer_t>(expansion) != nullptr) || expansion->eval_predicate(tb_zero, nominal_env, total_env);
	}

	void get_integer_attributes(
			status_t &status,
			type_t::ref type,
			const type_t::map &nominal_env,
			const type_t::map &total_env,
			unsigned &bit_size,
			bool &signed_)
	{
		type = type->eval(nominal_env, total_env);
		if (auto integer = dyncast<const type_integer_t>(type)) {
			type_t::ref bit_size_expansion;
			bit_size = coerce_to_integer(status, nominal_env, total_env, integer->bit_size, bit_size_expansion);
			if (!!status) {
				auto signed_type = integer->signed_->eval(nominal_env, total_env);
				if (types::is_type_id(signed_type, TRUE_TYPE, {}, {})) {
					signed_ = true;
					return;
				} else if (types::is_type_id(signed_type, FALSE_TYPE, {}, {})) {
					signed_ = false;
					return;
				} else {
					user_error(status, integer->signed_->get_location(), "unable to determine signedness for type from %s",
							signed_type->str().c_str());
				}
			}
		} else if (type->eval_predicate(tb_zero, nominal_env, total_env)) {
			bit_size = DEFAULT_INT_BITSIZE;
			signed_ = true;
			return;
		} else {
			user_error(status, type->get_location(), "expected an integer type, found %s",
					type->str().c_str());
		}

		assert(!status);
		return;
	}

	void get_runtime_typeids(
			status_t &status,
			type_t::ref type,
			const type_t::map &nominal_env,
			const type_t::map &total_env,
			std::set<int> &typeids)
	{
		auto expansion = type->eval(nominal_env, total_env);
		if (auto type_ref = dyncast<const type_ref_t>(expansion)) {
			user_error(status, type->get_location(), "reference types are not allowed here. %s does not have runtime type information",
					type->str().c_str());
		} else if (auto type_ptr = dyncast<const type_ptr_t>(expansion)) {
			user_error(status, type->get_location(), "pointer types are not allowed here. %s does not have runtime type information",
					type->str().c_str());
		} else if (auto type_id = dyncast<const type_id_t>(expansion)) {
			typeids.insert(atomize(type_id->repr()));
		} else if (auto type_sum = dyncast<const type_sum_t>(expansion)) {
			for (auto option : type_sum->options) {
				get_runtime_typeids(status, option, nominal_env, total_env, typeids);
				if (!status) {
					break;
				}
			}
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

types::type_t::ref type_unreachable() {
	static auto unreachable_type =  make_ptr<types::type_id_t>(make_iid(BUILTIN_UNREACHABLE_TYPE));
	return unreachable_type;
}

types::type_t::ref type_null() {
	static auto null_type = make_ptr<types::type_id_t>(make_iid(BUILTIN_NULL_TYPE));
	return null_type;
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

types::type_module_t::ref type_module(types::type_t::ref module_type) {
	return make_ptr<types::type_module_t>(module_type);
}

types::type_managed_t::ref type_managed(types::type_t::ref element_type) {
	return make_ptr<types::type_managed_t>(element_type);
}

types::type_function_t::ref type_function(
		identifier::ref name,
		types::type_t::ref type_constraints,
		types::type_t::ref args,
		types::type_t::ref return_type)
{
	return make_ptr<types::type_function_t>(name, type_constraints, args, return_type);
}

bool types_contains(const types::type_t::refs &options, std::string signature) {
	for (auto &option : options) {
		if (option->get_signature() == signature) {
			return true;
		}
	}
	return false;
}

void expand_options(
		types::type_t::refs &options,
	   	const types::type_t::refs &new_options,
		const types::type_t::map &nominal_env,
		const types::type_t::map &total_env)
{
	options.resize(0);
	for (auto option : new_options) {
		auto evaled = option->eval(nominal_env, total_env);
		if (auto sum_type = dyncast<const types::type_sum_t>(evaled)) {
			expand_options(options, sum_type->options, nominal_env, total_env);
		} else {
			options.push_back(option);
		}
	}
}


void eliminate_redundant_types(types::type_t::refs &options, const types::type_t::map &env, const types::type_t::map &total_env) {
	/* this function expects managed types, and will promote to managed types */
	if (options.size() == 0) {
		return;
	}
	for (int i=options.size() - 1; i >= 0 && options.size() > 1; --i) {
		if (dyncast<const types::type_variable_t>(options[i])) {
			/* if we have a free type variable, let's not eliminate anything... this needs more thought. */
			return;
		}
	}

	if (options.size() == 2) {
		auto &option0 = options[0];
		auto &option1 = options[1];
		if (
				(types::is_type_id(option0, MANAGED_TRUE, env, total_env)  && types::is_type_id(option1, MANAGED_FALSE, env, total_env)) ||
				(types::is_type_id(option0, MANAGED_FALSE, env, total_env) && types::is_type_id(option1, MANAGED_TRUE, env, total_env)))
		{
			/* any of the above combinations can be simplified */
			options = {type_id(make_iid(MANAGED_BOOL))};
			return;
		}
	}

	for (int i=options.size() - 1; i >= 0 && options.size() > 1; --i) {
		types::type_t::refs partial = options;
		std::swap(partial[i], partial[partial.size() - 1]);
		partial.resize(partial.size() - 1);

		assert(partial.size() == options.size() - 1);
		assert(partial.size() > 0);

		auto type_partial = type_sum(partial, INTERNAL_LOC());
		if (unifies(type_partial, options[i], env, total_env)) {
			/* options[i] is not needed */
			debug_above(8, log("removing one instance of type %s from %s", options[i]->str().c_str(),
						type_sum(options, INTERNAL_LOC())->str().c_str()));
			std::swap(options, partial);
		} else {
			debug_above(8, log("could not remove %s from %s", options[i]->str().c_str(),
						type_sum(options, INTERNAL_LOC())->str().c_str()));
		}
	}
}

types::type_t::ref demote_to_native_type(
		const types::type_t::ref &option,
	   	const types::type_t::map &nominal_env,
	   	const types::type_t::map &total_env)
{
	auto evaled = option->eval(nominal_env, total_env);
	if (types::is_type_id(evaled, MANAGED_BOOL, {}, {})) {
		return type_id(make_iid(BOOL_TYPE));
	} else if (types::is_type_id(evaled, MANAGED_INT, {}, {})) {
		return type_id(make_iid(INT_TYPE));
	} else if (types::is_type_id(evaled, MANAGED_FLOAT, {}, {})) {
		return type_id(make_iid(FLOAT_TYPE));
	} else if (types::is_type_id(evaled, MANAGED_TRUE, {}, {})) {
		return type_id(make_iid(TRUE_TYPE));
	} else if (types::is_type_id(evaled, MANAGED_FALSE, {}, {})) {
		return type_id(make_iid(FALSE_TYPE));
	} else {
		return option;
	}
}

types::type_t::ref promote_to_managed_type(
		const types::type_t::ref &option,
	   	const types::type_t::map &nominal_env,
	   	const types::type_t::map &total_env)
{
	if (types::is_managed_ptr(option, nominal_env, total_env)) {
		return option;
	}

	if (types::is_ptr_type_id(option, CHAR_TYPE, nominal_env, total_env)) {
		return type_id(make_iid_impl(MANAGED_STR, option->get_location()));
	}

	if (types::is_integer(option, nominal_env, total_env)) {
		return type_id(make_iid_impl(MANAGED_INT, option->get_location()));
	}

	static struct {
		const char * const native_type;
		const char * const managed_type;
	} coercions[] = {
		{INT_TYPE, MANAGED_INT},
		{ZERO_TYPE, MANAGED_INT},
		{FLOAT_TYPE, MANAGED_FLOAT},
		{BOOL_TYPE, MANAGED_BOOL},
		{TRUE_TYPE, MANAGED_TRUE},
		{FALSE_TYPE, MANAGED_FALSE},
	};

	for (unsigned i = 0; i < sizeof(coercions)/sizeof(coercions[0]); ++i) {
		/* coerce native types to managed types for the sake of maintaining polymorphism during (de)serialization */
		if (types::is_type_id(option, coercions[i].native_type, nominal_env, total_env)) {
			debug_above(9, log("coercing %s to %s for sum type", coercions[i].native_type, coercions[i].managed_type));
			return type_id(make_iid_impl(coercions[i].managed_type, option->get_location()));
		}
	}

	return option;
}

types::type_t::ref type_sum_safe_3(
		status_t &status,
        types::type_t::refs options,
        location_t location,
        const types::type_t::map &nominal_env,
		const types::type_t::map &total_env,
		bool make_maybe)
{
	debug_above(9, log("type_sum_safe_3(..., %s, %s, ..., ..., %s)",
				::str(options).c_str(), location.str().c_str(),
				boolstr(make_maybe)));

	/* check preconditions */
	for (auto option : options) {
		auto evaled = option->eval(nominal_env, total_env);
		assert(!types::is_type_id(evaled, BUILTIN_NULL_TYPE, nominal_env, total_env));
		assert(dyncast<const types::type_maybe_t>(evaled) == nullptr);
	}
	
	types::type_t::refs expanded_options;
	expand_options(expanded_options, options, nominal_env, total_env);
	for (auto &option : expanded_options) {
		option = promote_to_managed_type(option, nominal_env, total_env);
	}
	eliminate_redundant_types(expanded_options, nominal_env, total_env);

	if (expanded_options.size() == 0) {
		if (make_maybe) {
			return type_id(make_iid_impl("null", location));
		} else {
			user_error(status, location, "no type found");
		}
	} else if (expanded_options.size() == 1) {
		if (make_maybe) {
			return type_maybe(expanded_options[0]);
		} else {
			return demote_to_native_type(expanded_options[0], nominal_env, total_env);
		}
	} else if (expanded_options.size() >= 2) {
		if (make_maybe) {
			return type_maybe(type_sum(expanded_options, location));
		} else {
			return type_sum(expanded_options, location);
		}
	}
	assert(!status);
	return nullptr;
}

types::type_t::ref type_sum_safe_2(
		status_t &status,
        types::type_t::refs options,
        location_t location,
        const types::type_t::map &nominal_env,
		const types::type_t::map &total_env)
{
	assert(options.size() > 1);

	bool make_maybe = false;
	types::type_t::refs new_options;
	new_options.reserve(options.size());
	for (auto option : options) {
		auto evaled = option->eval(nominal_env, total_env);
		if (types::is_type_id(evaled, BUILTIN_NULL_TYPE, {}, {})) {
			make_maybe = true;
			continue;
		} else if (auto maybe = dyncast<const types::type_maybe_t>(evaled)) {
			make_maybe = true;
			new_options.push_back(maybe->just);
		} else {
			new_options.push_back(option);
		}
	}
	return type_sum_safe_3(status, new_options, location, nominal_env, total_env, make_maybe);
}

types::type_t::ref type_sum_safe(
		status_t &status,
        types::type_t::refs orig_options,
        location_t location,
        const types::type_t::map &nominal_env,
		const types::type_t::map &total_env)
{
	debug_above(9, log("type_sum_safe(%s, %s, ..., ...)", ::str(orig_options).c_str(), location.str().c_str()));
	types::type_t::refs options;
	expand_options(options, orig_options, nominal_env, total_env);

	if (options.size() == 1) {
		auto evaled = options[0]->eval(nominal_env, total_env);
		if (
				types::is_type_id(evaled, BUILTIN_NULL_TYPE, {}, {}) ||
				// types::is_type_id(evaled, "int") ||
				types::is_ptr_type_id(evaled, CHAR_TYPE, nominal_env, total_env, true) ||
				types::is_integer(evaled, nominal_env, total_env))
		{
			return options[0];
		} else {
			bool make_maybe = false;
			if (auto maybe = dyncast<const types::type_maybe_t>(evaled)) {
				make_maybe = true;
				evaled = maybe->just;
			}

			if (!make_maybe) {
				if (types::is_integer(evaled, nominal_env, total_env)) {
					return options[0];
				}
			}
			return type_sum_safe_3(status, {evaled}, location, nominal_env, total_env, make_maybe);
		}
	} else {
		return type_sum_safe_2(status, options, location, nominal_env, total_env);
	}
}


types::type_t::ref type_lazy(types::type_t::refs options, location_t location) {
	std::sort(
		options.begin(),
		options.end(),
		[] (const types::type_t::ref &lhs, const types::type_t::ref &rhs) -> bool {
			return lhs->repr() < rhs->repr();
		});
	return make_ptr<types::type_lazy_t>(options, location);
}

types::type_t::ref type_sum(types::type_t::refs options, location_t location) {
	std::sort(
		options.begin(),
		options.end(),
		[] (const types::type_t::ref &lhs, const types::type_t::ref &rhs) -> bool {
			return lhs->repr() < rhs->repr();
		});
	return make_ptr<types::type_sum_t>(options, location);
}

types::type_t::ref type_and(types::type_t::refs terms) {
	return make_ptr<types::type_and_t>(terms);
}

types::type_t::ref type_literal(token_t token) {
	assert(token.tk == tk_integer || token.tk == tk_string || token.tk == tk_identifier);
	return make_ptr<types::type_literal_t>(token);
}

types::type_t::ref type_integer(types::type_t::ref bit_size, types::type_t::ref signed_) {
	return make_ptr<types::type_integer_t>(bit_size, signed_);
}

types::type_t::ref type_maybe(types::type_t::ref just) {
    if (auto maybe = dyncast<const types::type_maybe_t>(just)) {
        return just;
    }

#if 0
	if (just->is_null()) {
		/* maybe of null is just null */
		return just;
	}
#endif

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

types::type_t::ref type_list_type(types::type_t::ref element) {
	return type_maybe(type_operator(type_id(make_iid_impl(
						STD_VECTOR_TYPE, element->get_location())), element));
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
		dimension->emit(os, bindings);
		sep = ", ";
	}
	return os;
}
