#include "zion.h"
#include "dbg.h"
#include "types.h"
#include <sstream>
#include "utils.h"
#include "types.h"
#include <iostream>
#include "unification.h"
#include "atom.h"
#include "env.h"
#include "encoding.h"
#include "ast.h"
#include "parens.h"
#include "user_error.h"

const char *NULL_TYPE = "null";
const char *STD_MANAGED_TYPE = "var_t";
const char *STD_VECTOR_TYPE = "vector.Vector";
const char *STD_MAP_TYPE = "map.Map";
const char *VOID_TYPE = "void";
const char *BOTTOM_TYPE = "⊥";

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

	std::shared_ptr<forall_t> type_t::generalize(env_t::ref env) const {
		std::vector<std::string> vs;
		auto type_ftvs = get_ftvs();
		auto env_ftvs = env.get_ftvs();
		for (auto ftv : type_ftvs) {
			if (!in(ftv, env_ftvs)) {
				vs.push_back(ftv);
			}
		}
		return forall(vs, shared_from_this());
	}

	type_id_t::type_id_t(identifier_t id) : id(id) {
		static bool seen_bottom = false;
		if (id.name.find(BOTTOM_TYPE) != std::string::npos) {
			assert(!seen_bottom);
			seen_bottom = true;
		}
	}

	std::ostream &type_id_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		return os << id.name;
	}

	void type_id_t::encode(env_t::ref env, std::vector<uint16_t> &encoding) const {
		encoding.push_back(atomize(id.name));
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

	type_t::ref type_id_t::unbottom() const {
		if (id.name == BOTTOM_TYPE) {
			return type_variable(INTERNAL_LOC());
		} else {
			return shared_from_this();
		}
	}

	location_t type_id_t::get_location() const {
		return id.location;
	}

	type_variable_t::type_variable_t(identifier_t id) : id(id), location(id.location) {
	}

	identifier_t gensym(location_t location) {
		/* generate fresh "any" variables */
		return identifier_t{string_format("__%d", next_generic++).c_str(), location};
	}

	type_variable_t::type_variable_t(location_t location) : id(types::gensym(location)), location(location) {
	}

	std::ostream &type_variable_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		auto instance_iter = bindings.find(id.name);
		if (instance_iter != bindings.end()) {
			assert(instance_iter->second != shared_from_this());
			return instance_iter->second->emit(os, bindings, parent_precedence);
		} else {
			return os << string_format("any %s", id.name.c_str());
		}
	}

	/* how many free type variables exist in this type? */
	int type_variable_t::ftv_count() const {
		return 1;
	}

	std::set<std::string> type_variable_t::get_ftvs() const {
		return {id.name};
	}

	type_t::ref type_variable_t::rebind(const map &bindings, bool bottom_out_free_vars) const {
		if (bindings.size() != 0) {
			auto instance_iter = bindings.find(id.name);
			if (instance_iter != bindings.end()) {
				/* recurse the rebinding, but remove the current rebinding */
				map new_bindings;
				for (auto &pair : bindings) {
					if (pair.first == id.name) {
						continue;
					}
					new_bindings.insert(pair);
				}
				return instance_iter->second->rebind(new_bindings);
			}
		}
		return bottom_out_free_vars ? type_bottom() : shared_from_this();
	}

	type_t::ref type_variable_t::unbottom() const {
		return shared_from_this();
	}

	location_t type_variable_t::get_location() const {
		return location;
	}

	type_operator_t::type_operator_t(type_t::ref oper, type_t::ref operand) :
		oper(oper), operand(operand)
	{
	}

	std::ostream &type_operator_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		if (is_type_id(oper->rebind(bindings), STD_VECTOR_TYPE, {})) {
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

	type_t::ref type_operator_t::unbottom() const {
		auto oper_ = oper->unbottom();
		auto operand_ = operand->unbottom();
		if (oper_ != oper || operand_ != operand) {
			return ::type_operator(oper_, operand_);
		} else {
			return shared_from_this();
		}
	}

	location_t type_operator_t::get_location() const {
		return oper->get_location();
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

	type_t::ref type_subtype_t::unbottom() const {
		assert(false);
		return shared_from_this();
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
		if (name_index.size() != dimensions.size() && name_index.size() != 0) {
			// mismatch in params here...
			std::cerr << ::join_str(dimensions, ", ") << std::endl;
			dbg();
		}
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

	type_t::ref type_struct_t::unbottom() const {
		// TODO: impl
		assert(false);
		return shared_from_this();
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

	type_t::ref type_tuple_t::unbottom() const {
		bool anything_was_rebound = false;
		refs type_dimensions;
		for (auto dimension : dimensions) {
			auto new_dim = dimension->unbottom();
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

	type_args_t::type_args_t(type_t::refs args, identifiers_t names) :
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

	type_t::ref type_args_t::unbottom() const {
		refs type_args;
		for (auto arg : args) {
			type_args.push_back(arg->unbottom());
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

	type_t::ref type_managed_t::unbottom() const {
		// TODO: impl
		assert(false);
		return shared_from_this();
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

	type_t::ref type_injection_t::unbottom() const {
		// TODO: impl
		assert(false);
		return shared_from_this();
	}

	location_t type_injection_t::get_location() const {
		return module_type->get_location();
	}

	type_and_t::type_and_t(type_t::refs terms) : terms(terms) {
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

	type_t::ref type_and_t::unbottom() const {
		// TODO: impl
		assert(false);
		return shared_from_this();
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

	type_t::ref type_eq_t::unbottom() const {
		// TODO: impl
		assert(false);
		return shared_from_this();
	}

	location_t type_eq_t::get_location() const {
		return location;
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

	type_t::ref type_ptr_t::unbottom() const {
		auto element_type_ = element_type->unbottom();
		if (element_type_ != element_type) {
			return std::make_shared<types::type_ptr_t>(element_type_);
		} else {
			return shared_from_this();
		}
	}

	location_t type_ptr_t::get_location() const {
		return element_type->get_location();
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

	type_t::ref type_ref_t::unbottom() const {
		// TODO: impl
		assert(false);
		return shared_from_this();
	}

	location_t type_ref_t::get_location() const {
		return element_type->get_location();
	}

	type_lambda_t::type_lambda_t(identifier_t binding, type_t::ref body) :
		binding(binding), body(body)
	{
	}

	std::ostream &type_lambda_t::emit(std::ostream &os, const map &bindings_, int parent_precedence) const {
		parens_t parens(os, parent_precedence, get_precedence());

		auto var_name = binding.name;
		auto new_name = gensym(get_location());
		os << "lambda " << new_name.name << " ";
		map bindings = bindings_;
		bindings[var_name] = type_id(new_name);
		body->emit(os, bindings, get_precedence());
		return os;
	}

	int type_lambda_t::ftv_count() const {
		/* pretend this is getting applied */
		map bindings;
		bindings[binding.name] = type_bottom();
		return body->rebind(bindings)->ftv_count();
	}

	std::set<std::string> type_lambda_t::get_ftvs() const {
		map bindings;
		bindings[binding.name] = type_bottom();
		return body->rebind(bindings)->get_ftvs();
	}

	type_t::ref type_lambda_t::rebind(const map &bindings_, bool bottom_out_free_vars) const {
		if (bindings_.size() == 0) {
			return shared_from_this();
		}

		map bindings = bindings_;
		auto binding_iter = bindings.find(binding.name);
		if (binding_iter != bindings.end()) {
			bindings.erase(binding_iter);
		}
		return ::type_lambda(binding, body->rebind(bindings, bottom_out_free_vars));
	}

	type_t::ref type_lambda_t::unbottom() const {
		// TODO: impl
		assert(false);
		return shared_from_this();
	}

	location_t type_lambda_t::get_location() const {
		return binding.location;
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

	type_t::ref type_integer_t::unbottom() const {
		return shared_from_this();
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

	type_t::ref type_literal_t::unbottom() const {
		// TODO: impl
		assert(false);
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

	type_t::ref type_extern_t::unbottom() const {
		// TODO: impl
		assert(false);
		return shared_from_this();
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
		if (name.text == MAYBE_TYPE) {
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
		os << "/" << ctor_pairs.size();
#if 0
		os << " " << K(is);
		for (auto ctor_pair : ctor_pairs) {
			os << " ";
			os << ctor_pair.first.text;
			if (ctor_pair.second->args.size() != 0) {
				ctor_pair.second->emit(os, bindings, get_precedence());
			}
		}
#endif
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

	type_t::ref type_data_t::unbottom() const {
		bool found_new = false;
		type_variable_t::refs new_type_vars;
		new_type_vars.reserve(type_vars.size());
		for (auto type_var : type_vars) {
			new_type_vars.push_back(type_var->unbottom());
			if (new_type_vars.back() != type_var) {
				found_new = true;
			}
		}

		std::vector<std::pair<token_t, type_args_t::ref>> new_ctor_pairs;
		for (auto ctor_pair : ctor_pairs) {
			types::type_args_t::ref elem = dyncast<const type_args_t>(ctor_pair.second->unbottom());
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

	bool is_type_id(type_t::ref type, const std::string &type_name, env_t::ref env) {
		type = type->eval(env);

		if (auto pti = dyncast<const types::type_id_t>(type)) {
			return pti->id.name == type_name;
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

	bool is_integer(type_t::ref type, env_t::ref env) {
		auto expansion = type->eval(env);
		return dyncast<const type_integer_t>(expansion) != nullptr;
	}

	void get_integer_attributes(
			location_t location,
			type_integer_t::ref integer,
			env_t::ref env,
			unsigned &bit_size,
			bool &signed_)
	{
		type_t::ref bit_size_expansion;
		bit_size = coerce_to_integer(env, integer->bit_size, bit_size_expansion);
		auto signed_type = integer->signed_->eval(env);
		if (types::is_type_id(signed_type, TRUE_TYPE, {})) {
			signed_ = true;
			return;
		} else if (types::is_type_id(signed_type, FALSE_TYPE, {})) {
			signed_ = false;
			return;
		} else {
			throw user_error(integer->signed_->get_location(), "unable to determine signedness for type from %s",
					signed_type->str().c_str());
		}
	}

	bool maybe_get_integer_attributes(
			location_t location,
			type_t::ref type,
			env_t::ref env,
			unsigned &bit_size,
			bool &signed_)
	{
		type = type->eval(env);
		if (auto integer = dyncast<const type_integer_t>(type)) {
			get_integer_attributes(location, integer, env, bit_size, signed_);
			return true;
		} else if (types::is_type_id(type, CHAR_TYPE, {})) {
			bit_size = 8;
			signed_ = false;
			return true;
		} else {
			return false;
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
		assert(lhs != nullptr);
		assert(rhs != nullptr);
		std::set<std::string> shared_ftvs;
		auto lhs_ftvs = lhs->get_ftvs();
		auto rhs_ftvs = rhs->get_ftvs();
		std::set_intersection(
				lhs_ftvs.begin(), lhs_ftvs.end(),
				rhs_ftvs.begin(), rhs_ftvs.end(),
				std::insert_iterator<std::set<std::string>>(shared_ftvs, shared_ftvs.begin()));

		return shared_ftvs.size() != 0;
	}

	types::type_t::ref forall_t::instantiate(location_t location) {
		type_t::map subst;
		for (auto var : vars) {
			subst[var] = type_variable(location);
		}
		return type->rebind(subst);
	}
	std::set<std::string> forall_t::get_ftvs() {
		std::set<std::string> ftvs;
		for (auto ftv : type->get_ftvs()) {
			if (!in_vector(ftv, vars)) {
				ftvs.insert(ftv);
			}
		}
		return ftvs;
	}
}

types::type_t::ref type_id(identifier_t id) {
	return std::make_shared<types::type_id_t>(id);
}

types::type_t::ref type_variable(identifier_t id) {
	return std::make_shared<types::type_variable_t>(id);
}

types::type_t::ref type_variable(location_t location) {
	return std::make_shared<types::type_variable_t>(location);
}

types::type_t::ref type_unit() {
    return type_tuple({});
}

types::type_t::ref type_bottom() {
	static auto bottom_type = std::make_shared<types::type_id_t>(make_iid(BOTTOM_TYPE));
	return bottom_type;
}

types::type_t::ref type_bool() {
	static auto bool_type = std::make_shared<types::type_id_t>(make_iid(BOOL_TYPE));
	return bool_type;
}

types::type_t::ref type_string() {
	static auto string_type = std::make_shared<types::type_id_t>(make_iid(STR_TYPE));
	return string_type;
}

types::type_t::ref type_int() {
	static auto int_type = std::make_shared<types::type_id_t>(make_iid(INT_TYPE));
	return int_type;
}

types::type_t::ref type_null() {
	static auto null_type = std::make_shared<types::type_id_t>(make_iid(NULL_TYPE));
	return null_type;
}

types::type_t::ref type_void() {
	return std::make_shared<types::type_id_t>(make_iid(VOID_TYPE));
}

types::type_t::ref type_operator(types::type_t::ref operator_, types::type_t::ref operand) {
	return std::make_shared<types::type_operator_t>(operator_, operand);
}

types::type_t::ref type_subtype(types::type_t::ref lhs, types::type_t::ref rhs) {
	return std::make_shared<types::type_subtype_t>(lhs, rhs);
}

types::forall_t::ref forall(std::vector<std::string> vars, types::type_t::ref type) {
	return std::make_shared<types::forall_t>(vars, type);
}

types::name_index_t get_name_index_from_ids(identifiers_t ids) {
	types::name_index_t name_index;
	int i = 0;
	for (auto id : ids) {
		name_index[id.name] = i++;
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
	return std::make_shared<types::type_struct_t>(dimensions, name_index);
}

types::type_tuple_t::ref type_tuple(types::type_t::refs dimensions) {
	return std::make_shared<types::type_tuple_t>(dimensions);
}

types::type_args_t::ref type_args(
	   	types::type_t::refs args,
		const identifiers_t &names)
{
	assert((names.size() == args.size()) ^ (names.size() == 0 && args.size() != 0));
	for (auto arg : args) {
		assert(dyncast<const types::type_ref_t>(arg) == nullptr);
	}
	return std::make_shared<types::type_args_t>(args, names);
}

types::type_injection_t::ref type_injection(types::type_t::ref module_type) {
	return std::make_shared<types::type_injection_t>(module_type);
}

types::type_managed_t::ref type_managed(types::type_t::ref element_type) {
	return std::make_shared<types::type_managed_t>(element_type);
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
	return std::make_shared<types::type_and_t>(terms);
}

types::type_t::ref type_eq(types::type_t::ref lhs, types::type_t::ref rhs, location_t location) {
	return std::make_shared<types::type_eq_t>(lhs, rhs, location);
}

types::type_t::ref type_arrow(types::type_t::ref a, types::type_t::ref b) {
	return type_operator(type_operator(type_id(identifier_t{"->", INTERNAL_LOC()}), a), b);
}

types::type_t::ref type_literal(token_t token) {
	assert(token.tk == tk_integer || token.tk == tk_string || token.tk == tk_identifier);
	return std::make_shared<types::type_literal_t>(token);
}

types::type_t::ref type_integer(types::type_t::ref bit_size, types::type_t::ref signed_) {
	return std::make_shared<types::type_integer_t>(bit_size, signed_);
}

types::type_ptr_t::ref type_ptr(types::type_t::ref raw) {
    return std::make_shared<types::type_ptr_t>(raw);
}

types::type_t::ref type_ref(types::type_t::ref raw) {
    assert(!dyncast<const types::type_ref_t>(raw));
    return std::make_shared<types::type_ref_t>(raw);
}

types::type_t::ref type_lambda(identifier_t binding, types::type_t::ref body) {
    return std::make_shared<types::type_lambda_t>(binding, body);
}

types::type_t::ref type_extern(types::type_t::ref inner)
{
    return std::make_shared<types::type_extern_t>(inner);
}

types::type_t::ref type_data(
		token_t name,
	   	types::type_variable_t::refs type_vars,
	   	std::vector<std::pair<token_t, types::type_args_t::ref>> ctor_pairs)
{
	return std::make_shared<types::type_data_t>(name, type_vars, ctor_pairs);
}

types::type_t::ref type_vector_type(types::type_t::ref element) {
	return type_operator(type_id(identifier_t{
					STD_VECTOR_TYPE, element->get_location()}), element);
}

std::ostream& operator <<(std::ostream &os, const types::type_t::ref &type) {
	os << type->str();
	return os;
}

bool get_type_variable_name(types::type_t::ref type, std::string &name) {
    if (auto ptv = dyncast<const types::type_variable_t>(type)) {
		name = ptv->id.name;
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
