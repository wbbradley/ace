#include "zion.h"
#include "dbg.h"
#include "types.h"
#include <sstream>
#include "utils.h"
#include "types.h"
#include <iostream>
#include "unification.h"
#include "env.h"
#include "ast.h"
#include "parens.h"
#include "user_error.h"
#include "prefix.h"

const char *NULL_TYPE = "null";
const char *STD_MANAGED_TYPE = "var_t";
const char *STD_MAP_TYPE = "map.Map";
const char *VOID_TYPE = "void";
const char *BOTTOM_TYPE = "⊥";

int next_generic = 1;

void reset_generics() {
	next_generic = 1;
}

identifier_t gensym(location_t location) {
	/* generate fresh "any" variables */
	return identifier_t{string_format("__%s", alphabetize(next_generic++).c_str()).c_str(), location};
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

void mutating_merge(const types::predicate_map::value_type &pair, types::predicate_map &c) {
	if (!in(pair.first, c)) {
		c.insert(pair);
	} else {
		for (auto predicate : pair.second) {
			c[pair.first].insert(predicate);
		}
	}
}
void mutating_merge(const types::predicate_map &a, types::predicate_map &c) {
	for (auto pair : a) {
		mutating_merge(pair, c);
	}
}

types::predicate_map merge(const types::predicate_map &a, const types::predicate_map &b) {
	types::predicate_map c;
	mutating_merge(a, c);
	mutating_merge(b, c);
	return c;
}

types::predicate_map safe_merge(const types::predicate_map &a, const types::predicate_map &b) {
	types::predicate_map c;
	mutating_merge(a, c);
	for (auto pair : b) {
		assert(!in(pair.first, c));
	}
	mutating_merge(b, c);
	return c;
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
		emit(ss, bindings, 0);
		return ss.str();
	}

	std::shared_ptr<scheme_t> type_t::generalize(const types::predicate_map &pm) const {
		std::vector<std::string> vs;
		auto type_ftvs = get_predicate_map();
		// auto env_ftvs = env.get_predicate_map();
		predicate_map predicate_map;
		type_t::map bindings;
		for (auto ftv : type_ftvs) {
			if (!in(ftv.first, pm)) {
				vs.push_back(ftv.first);
				mutating_merge(ftv, predicate_map);
				bindings[ftv.first] = type_variable(make_iid(ftv.first));
			}
		}
		return scheme(vs, predicate_map, rebind(bindings));
	}

	type_t::ref type_t::apply(types::type_t::ref type) const {
		assert(false);
		return type_operator(shared_from_this(), type);
	}

	type_id_t::type_id_t(identifier_t id) : id(id) {
		auto dot_index = id.name.find(".");
		if (dot_index == std::string::npos) {
			dot_index = 0;
		}
		assert(id.name.size() > dot_index);
		if (islower(id.name[dot_index])) {
			throw user_error(id.location, "type identifiers must begin with an upper-case letter");
		}

		static bool seen_bottom = false;
		if (id.name.find(BOTTOM_TYPE) != std::string::npos) {
			assert(!seen_bottom);
			seen_bottom = true;
		}
	}

	std::ostream &type_id_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		return os << id.name;
	}

	int type_id_t::ftv_count() const {
		/* how many free type variables exist in this type? */
		return 0;
	}

	predicate_map type_id_t::get_predicate_map() const {
		return {};
	}

	type_t::ref type_id_t::rebind(const map &bindings) const {
		return shared_from_this();
	}

	type_t::ref type_id_t::remap_vars(const std::map<std::string, std::string> &map) const {
		return shared_from_this();
	}

	type_t::ref type_id_t::prefix_ids(const std::set<std::string> &bindings, const std::string &pre) const {
		if (in(id.name, bindings)) {
			return type_id(prefix(bindings, pre, id));
		} else {
			return shared_from_this();
		}
	}

	location_t type_id_t::get_location() const {
		return id.location;
	}

	type_variable_t::type_variable_t(identifier_t id, std::set<std::string> predicates) :
		id(id), predicates(predicates)
	{
		for (auto ch : id.name) {
			assert(islower(ch) || !isalpha(ch));
		}
	}

	type_variable_t::type_variable_t(identifier_t id) : type_variable_t(id, {}) {}

	type_variable_t::type_variable_t(location_t location) : id(gensym(location)) {
		for (auto ch : id.name) {
			assert(islower(ch) || !isalpha(ch));
		}
	}

	std::ostream &type_variable_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		auto instance_iter = bindings.find(id.name);
		if (instance_iter != bindings.end()) {
			assert(instance_iter->second != shared_from_this());
			return instance_iter->second->emit(os, bindings, parent_precedence);
		} else {
			if (predicates.size() == 0) {
				return os << string_format("%s", id.name.c_str());
			} else {
				return os << string_format("%s|[%s]", id.name.c_str(), join(predicates, ", ").c_str());
			}
		}
	}

	/* how many free type variables exist in this type? */
	int type_variable_t::ftv_count() const {
		return 1;
	}

	predicate_map type_variable_t::get_predicate_map() const {
		predicate_map pm;
		pm[id.name] = predicates;
		return pm;
	}

	type_t::ref type_variable_t::rebind(const map &bindings) const {
		return get(bindings, id.name, shared_from_this());
	}

	type_t::ref type_variable_t::remap_vars(const std::map<std::string, std::string> &map) const {
		auto iter = map.find(id.name);
		assert(iter != map.end());
		return type_variable(identifier_t{iter->second, id.location});
	}

	type_t::ref type_variable_t::prefix_ids(const std::set<std::string> &bindings, const std::string &pre) const {
		return type_variable(id, prefix(bindings, pre, predicates));
	}

	location_t type_variable_t::get_location() const {
		return id.location;
	}

	type_operator_t::type_operator_t(type_t::ref oper, type_t::ref operand) :
		oper(oper), operand(operand)
	{
	}

	std::ostream &type_operator_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		if (is_type_id(oper->rebind(bindings), VECTOR_TYPE)) {
			os << "[";
			operand->emit(os, bindings, 0);
			return os << "]";
		} else {
			parens_t parens(os, parent_precedence, get_precedence());
			auto rebound_oper = oper->rebind(bindings);
			if (auto op = dyncast<const type_operator_t>(rebound_oper)) {
				if (auto inner_op = dyncast<const type_id_t>(op->oper)) {
					if (strspn(inner_op->id.name.c_str(), MATHY_SYMBOLS) == inner_op->id.name.size()) {
						op->operand->emit(os, {}, get_precedence());
						os << " " << inner_op->id.name << " ";
						return operand->emit(os, bindings, get_precedence());
					}
				}
			}
			oper->emit(os, bindings, get_precedence());
			os << " ";
			operand->emit(os, bindings, get_precedence() + 1);
			return os;
		}
	}

	int type_operator_t::ftv_count() const {
		return oper->ftv_count() + operand->ftv_count();
	}

	predicate_map type_operator_t::get_predicate_map() const {
		return merge(oper->get_predicate_map(), operand->get_predicate_map());
	}

	type_t::ref type_operator_t::rebind(const map &bindings) const {
		if (bindings.size() == 0) {
			return shared_from_this();
		}

		return ::type_operator(oper->rebind(bindings), operand->rebind(bindings));
	}

	type_t::ref type_operator_t::remap_vars(const std::map<std::string, std::string> &map) const {
		return ::type_operator(oper->remap_vars(map), operand->remap_vars(map));
	}

	type_t::ref type_operator_t::prefix_ids(const std::set<std::string> &bindings, const std::string &pre) const {
		return ::type_operator(oper->prefix_ids(bindings, pre), operand->prefix_ids(bindings, pre));
	}

	location_t type_operator_t::get_location() const {
		return oper->get_location();
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

	std::ostream &type_tuple_t::emit(std::ostream &os, const map &bindings, int parent_precedence) const {
		os << "(";
		join_dimensions(os, dimensions, {}, bindings);
		if (dimensions.size() != 0) {
			os << ",";
		}
		return os << ")";
	}

	int type_tuple_t::ftv_count() const {
		int ftv_sum = 0;
		for (auto dimension : dimensions) {
			ftv_sum += dimension->ftv_count();
		}
		return ftv_sum;
	}

	predicate_map type_tuple_t::get_predicate_map() const {
		predicate_map pm;
		for (auto dimension : dimensions) {
			mutating_merge(dimension->get_predicate_map(), pm);
		}
		return pm;
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

	type_t::ref type_tuple_t::remap_vars(const std::map<std::string, std::string> &map) const {
		bool anything_was_rebound = false;
		refs type_dimensions;
		for (auto dimension : dimensions) {
			auto new_dim = dimension->remap_vars(map);
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

	type_t::ref type_tuple_t::prefix_ids(const std::set<std::string> &bindings, const std::string &pre) const {
		bool anything_was_rebound = false;
		refs type_dimensions;
		for (auto dimension : dimensions) {
			auto new_dim = dimension->prefix_ids(bindings, pre);
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

	type_lambda_t::type_lambda_t(identifier_t binding, type_t::ref body) :
		binding(binding), body(body)
	{
		assert(islower(binding.name[0]));
	}

	std::ostream &type_lambda_t::emit(std::ostream &os, const map &bindings_, int parent_precedence) const {
		parens_t parens(os, parent_precedence, get_precedence());

		auto var_name = binding.name;
		auto new_name = gensym(get_location());
		os << "Λ " << new_name.name << " . ";
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

	predicate_map type_lambda_t::get_predicate_map() const {
		assert(false);
		return {};
#if 0
		map bindings;
		bindings[binding.name] = type_bottom();
		return body->rebind(bindings)->get_predicate_map();
#endif
	}

	type_t::ref type_lambda_t::rebind(const map &bindings_) const {
		if (bindings_.size() == 0) {
			return shared_from_this();
		}

		map bindings = bindings_;
		auto binding_iter = bindings.find(binding.name);
		if (binding_iter != bindings.end()) {
			bindings.erase(binding_iter);
		}
		return ::type_lambda(binding, body->rebind(bindings));
	}

	type_t::ref type_lambda_t::remap_vars(const std::map<std::string, std::string> &map_) const {
		assert(false);
		if (in(binding.name, map_)) {
			std::map<std::string, std::string> map = map_;
			auto new_binding = alphabetize(map.size());
			map[binding.name] = new_binding;
			assert(!in(new_binding, map_));
			assert(!in(new_binding, get_predicate_map()));
			return ::type_lambda(
					identifier_t{new_binding, binding.location},
					body->remap_vars(map));
		}
		return ::type_lambda(binding, body->remap_vars(map_));
	}

	type_t::ref type_lambda_t::prefix_ids(const std::set<std::string> &bindings, const std::string &pre) const {
		return type_lambda(binding, body->prefix_ids(without(bindings, binding.name), pre));
	}

	type_t::ref type_lambda_t::apply(types::type_t::ref type) const {
		map bindings;
		bindings[binding.name] = type;
		return body->rebind(bindings);
	}

	location_t type_lambda_t::get_location() const {
		return binding.location;
	}

	bool is_type_id(type_t::ref type, const std::string &type_name) {
		if (auto pti = dyncast<const types::type_id_t>(type)) {
			return pti->id.name == type_name;
		}

		return false;
	}

	type_t::refs rebind(const type_t::refs &types, const type_t::map &bindings) {
		type_t::refs rebound_types;
		for (const auto &type : types) {
			rebound_types.push_back(type->rebind(bindings));
		}
		return rebound_types;
	}

	types::type_t::ref scheme_t::instantiate(location_t location) {
		type_t::map subst;
		for (auto var : vars) {
			subst[var] = type_variable(gensym(location), predicates[var]);
		}
		return type->rebind(subst);
	}

	type_t::map remove_bindings(const type_t::map &env, const std::vector<std::string> &vars) {
		type_t::map new_map{env};
		for (auto var : vars) {
			new_map.erase(var);
		}
		return new_map;
	}

	scheme_t::ref scheme_t::rebind(const types::type_t::map &bindings) {
		/* this is subtle because it actually rebinds type variables that are free within the
		 * not-yet-normalized scheme. This is because the map containing the schemes is a working
		 * set of types that are waiting to be bound. In some cases the variability of the inner
		 * types can be resolved. */
		return scheme(vars, predicates, type->rebind(remove_bindings(bindings, vars)));
	}

	scheme_t::ref scheme_t::normalize() {
		std::map<std::string, std::string> ord;
		predicate_map pm;

		int counter = 0;
		for (auto ftv: type->get_predicate_map()) {
			auto new_name = alphabetize(counter++);
			ord[ftv.first] = new_name;
			if (in(ftv.first, predicates)) {
				pm[new_name] = predicates[ftv.first];
			}
		}
		return scheme(values(ord), pm, type->remap_vars(ord));
	}

	std::string scheme_t::str() {
		std::stringstream ss;
		if (vars.size() != 0) {
			ss << "(∀ " << join(vars, " ");
			ss << ::str(predicates);
			ss << " . ";
		}
		type->emit(ss, {}, 0);
		if (vars.size() != 0) {
			ss << ")";
		}
		return ss.str();
	}

	int scheme_t::btvs() const {
		int sum = 0;
		for (auto pair : predicates) {
			sum += pair.second.size();
		}
		return sum;
	}

	predicate_map scheme_t::get_predicate_map() {
		predicate_map predicate_map = type->get_predicate_map();
		for (auto var : vars) {
			predicate_map.erase(var);
		}
		return predicate_map;
	}

	location_t scheme_t::get_location() const {
		return type->get_location();
	}
}

types::type_t::ref type_id(identifier_t id) {
	return std::make_shared<types::type_id_t>(id);
}

types::type_t::ref type_variable(identifier_t id) {
	return std::make_shared<types::type_variable_t>(id);
}

types::type_t::ref type_variable(identifier_t id, const std::set<std::string> &predicates) {
	return std::make_shared<types::type_variable_t>(id, predicates);
}

types::type_t::ref type_variable(location_t location) {
	return std::make_shared<types::type_variable_t>(location);
}

types::type_t::ref type_unit(location_t location) {
    return type_tuple({});
}

types::type_t::ref type_bottom() {
	static auto bottom_type = std::make_shared<types::type_id_t>(make_iid(BOTTOM_TYPE));
	return bottom_type;
}

types::type_t::ref type_bool(location_t location) {
	return std::make_shared<types::type_id_t>(identifier_t{BOOL_TYPE, location});
}

types::type_t::ref type_vector_type(types::type_t::ref element) {
	return type_operator(type_id(identifier_t{
					VECTOR_TYPE, element->get_location()}), element);
}

types::type_t::ref type_string(location_t location) {
	return type_vector_type(type_id(identifier_t{CHAR_TYPE, location}));
}

types::type_t::ref type_int(location_t location) {
	return std::make_shared<types::type_id_t>(identifier_t{INT_TYPE, location});
}

types::type_t::ref type_null(location_t location) {
	return std::make_shared<types::type_id_t>(identifier_t{NULL_TYPE, location});
}

types::type_t::ref type_void(location_t location) {
	return std::make_shared<types::type_id_t>(identifier_t{VOID_TYPE, location});
}

types::type_t::ref type_operator(types::type_t::ref operator_, types::type_t::ref operand) {
	return std::make_shared<types::type_operator_t>(operator_, operand);
}

types::type_t::ref type_operator(const types::type_t::refs &xs) {
	assert(xs.size() >= 2);
	types::type_t::ref result = type_operator(xs[0], xs[1]);
	for (int i=2; i<xs.size(); ++i) {
		result = type_operator(result, xs[i]);
	}
	return result;
}

types::scheme_t::ref scheme(
		std::vector<std::string> vars,
	   	const types::predicate_map &predicates,
	   	types::type_t::ref type)
{
	assert(type->str().find("|") == std::string::npos);
	return std::make_shared<types::scheme_t>(vars, predicates, type);
}

types::name_index_t get_name_index_from_ids(identifiers_t ids) {
	types::name_index_t name_index;
	int i = 0;
	for (auto id : ids) {
		name_index[id.name] = i++;
	}
	return name_index;
}

types::type_t::ref type_map(types::type_t::ref a, types::type_t::ref b) {
	return type_operator(type_operator(type_id(identifier_t{"Map", a->get_location()}), a), b);
}

types::type_tuple_t::ref type_tuple(types::type_t::refs dimensions) {
	return std::make_shared<types::type_tuple_t>(dimensions);
}

types::type_t::ref type_arrow(types::type_t::ref a, types::type_t::ref b) {
	return type_arrow(a->get_location(), a, b);
}

types::type_t::ref type_arrow(location_t location, types::type_t::ref a, types::type_t::ref b) {
	return type_operator(type_operator(type_id(identifier_t{ARROW_TYPE_OPERATOR, location}), a), b);
}

types::type_t::ref type_arrows(types::type_t::refs types, int offset) {
	assert(types.size() - offset > 0);
	if (types.size() - offset == 1) {
		return types[offset];
	} else {
		return type_arrow(types[offset]->get_location(), types[offset], type_arrows(types, offset + 1));
	}
}

types::type_t::ref type_ptr(types::type_t::ref raw) {
	return type_operator(type_id(identifier_t{PTR_TYPE_OPERATOR, raw->get_location()}), raw);
}

types::type_t::ref type_lambda(identifier_t binding, types::type_t::ref body) {
    return std::make_shared<types::type_lambda_t>(binding, body);
}

types::type_t::ref type_tuple_accessor(int i, int max, const std::vector<std::string> &vars) {
	types::type_t::refs dims;
	for (int j=0; j<max; ++j) {
		dims.push_back(type_variable(make_iid(vars[j])));
	}
	return type_arrows({type_tuple(dims), type_variable(make_iid(vars[i]))});
}

std::ostream& operator <<(std::ostream &os, const types::type_t::ref &type) {
	os << type->str();
	return os;
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

std::string str(const types::predicate_map &pm) {
	bool saw_predicate = false;
	std::stringstream ss;
	const char *delim = " [where ";
	for (auto pair : pm) {
		for (auto predicate: pair.second) {
			ss << delim;
			ss << predicate << " " << pair.first;
			delim = ", ";
			saw_predicate = true;
		}
	}
	if (saw_predicate) {
		ss << "]";
	}

	return ss.str();
}

std::string str(const data_ctors_map_t &data_ctors_map) {
	std::stringstream ss;
	const char *delim = "";
	for (auto pair : data_ctors_map) {
		ss << delim << pair.first << ": " << ::str(pair.second);
		delim = ", ";
	}
	return ss.str();
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

void unfold_binops_rassoc(std::string id, types::type_t::ref t, types::type_t::refs &unfolding) {
	auto op = dyncast<const types::type_operator_t>(t);
	if (op != nullptr) {
		auto nested_op = dyncast<const types::type_operator_t>(op->oper);
		if (nested_op != nullptr) {
			if (is_type_id(nested_op->oper, id)) {
				unfolding.push_back(nested_op->operand);
				unfold_binops_rassoc(id, op->operand, unfolding);
				return;
			}
		}
	}
	unfolding.push_back(t);
}

void unfold_ops_lassoc(types::type_t::ref t, types::type_t::refs &unfolding) {
	auto op = dyncast<const types::type_operator_t>(t);
	if (op != nullptr) {
		unfold_ops_lassoc(op->oper, unfolding);
		unfolding.push_back(op->operand);
	} else {
		unfolding.push_back(t);
	}
}

void insert_needed_defn(needed_defns_t &needed_defns, const defn_id_t &defn_id, location_t location, const defn_id_t &for_defn_id) {
	needed_defns[defn_id].push_back({location, for_defn_id});
}

types::type_t::ref type_deref(location_t location, types::type_t::ref type) {
	auto ptr = safe_dyncast<const types::type_operator_t>(type);
	if (types::is_type_id(ptr->oper, PTR_TYPE_OPERATOR)) {
		return ptr->operand;
	} else {
		throw user_error(location, "attempt to dereference value of type %s", type->str().c_str());
	}
}

types::type_t::ref tuple_deref_type(location_t location, types::type_t::ref tuple_, int index) {
	auto tuple = safe_dyncast<const types::type_tuple_t>(tuple_);
	if (tuple->dimensions.size() < index || index < 0) {
		auto error = user_error(location, "attempt to access type of element at index %d which is out of range", index);
		error.add_info(tuple_->get_location(), "type is %s", tuple_->str().c_str());
		throw error;
	}
	return tuple->dimensions[index];
}
