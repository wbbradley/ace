#include "types.h"

#include <iostream>
#include <sstream>

#include "ast.h"
#include "builtins.h"
#include "class_predicate.h"
#include "dbg.h"
#include "env.h"
#include "parens.h"
#include "prefix.h"
#include "types.h"
#include "unification.h"
#include "user_error.h"
#include "utils.h"
#include "zion.h"

const char *NULL_TYPE = "null";
const char *STD_MANAGED_TYPE = "Var";
const char *STD_MAP_TYPE = "map.Map";
const char *VOID_TYPE = "void";
const char *BOTTOM_TYPE = "⊥";

int next_generic = 1;

std::string gensym_name() {
  return string_format("__%s", alphabetize(next_generic++).c_str());
}

Identifier gensym(Location location) {
  /* generate fresh variable names */
  return Identifier{gensym_name(), location};
}

std::string get_name_from_index(const types::NameIndex &name_index, int i) {
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

int Type::ftv_count() const {
  return get_ftvs().size();
}

const Ftvs &Type::get_ftvs() const {
  /* maintain this object's predicate map cache */
  if (!ftvs_valid_) {
    /* call into derived classes */
    this->compute_ftvs();

    ftvs_valid_ = true;
  }

  return ftvs_;
}

std::string Type::str() const {
  return str(Map{});
}

std::string Type::str(const Map &bindings) const {
  return string_format(c_type("%s"), this->repr(bindings).c_str());
}

std::string Type::repr(const Map &bindings) const {
  std::stringstream ss;
  emit(ss, bindings, 0);
  return ss.str();
}

types::ClassPredicates get_overlapping_predicates(
    const types::ClassPredicates &cps,
    const Ftvs &ftvs,
    Ftvs *overlapping_ftvs) {
  /* eliminate class predicates that do not mention any ftvs. fill out the
   * |overlapping_ftvs|.  */
  if (cps.size() != 0 || ftvs.size() != 0) {
    log("looking for overlapping predicates between {%s} and {%s}",
        join_str(cps, ", ").c_str(), join(ftvs, ", ").c_str());
  }
  types::ClassPredicates new_cps;
  Ftvs existing_ftvs;
  for (auto &cp : cps) {
    const Ftvs &cp_ftvs = cp->get_ftvs();
    if (any_in(ftvs, cp_ftvs)) {
      set_concat(existing_ftvs, cp_ftvs);
      new_cps.insert(cp);
    }
  }
  if (overlapping_ftvs != nullptr) {
    overlapping_ftvs->clear();
    std::swap(*overlapping_ftvs, existing_ftvs);
  }
  if (cps.size() != 0) {
    log("found new cps %s", str(new_cps).c_str());
  }
  return new_cps;
}

Ftvs get_ftvs(const types::ClassPredicates &cps) {
  Ftvs ftvs;
  for (auto &cp : cps) {
    set_concat(ftvs, cp->get_ftvs());
  }
  return ftvs;
}

std::shared_ptr<const Scheme> Type::generalize(
    const types::ClassPredicates &pm) const {
  Ftvs this_ftvs = this->get_ftvs();
  Ftvs overlapping_ftvs;
  ClassPredicates new_predicates = get_overlapping_predicates(
      pm, this_ftvs, &overlapping_ftvs);
  std::vector<std::string> vs;
  for (auto &ftv : this_ftvs) {
    /* make sure all the type variables are accounted for */
    vs.push_back(ftv);
  }
  return scheme(vs, new_predicates, shared_from_this());
}

Ref Type::apply(types::Ref type) const {
  assert(false);
  return type_operator(shared_from_this(), type);
}

TypeId::TypeId(Identifier id) : id(id) {
  auto dot_index = id.name.find(".");
  if (dot_index == std::string::npos) {
    dot_index = 0;
  }
  assert(id.name.size() > dot_index);
  if (islower(id.name[dot_index])) {
    throw user_error(id.location,
                     "type identifiers must begin with an upper-case letter");
  }

  static bool seen_bottom = false;
  if (id.name.find(BOTTOM_TYPE) != std::string::npos) {
    assert(!seen_bottom);
    seen_bottom = true;
  }
}

std::ostream &TypeId::emit(std::ostream &os,
                           const Map &bindings,
                           int parent_precedence) const {
  return os << id.name;
}

void TypeId::compute_ftvs() const {
}

Ref TypeId::eval(const TypeEnv &type_env) const {
  debug_above(5, log("trying to get %s from type_env {%s}", id.name.c_str(),
                     ::str(type_env).c_str()));
  return get(type_env, id.name, shared_from_this());
}

Ref TypeId::rebind(const Map &bindings) const {
  return shared_from_this();
}

Ref TypeId::remap_vars(const std::map<std::string, std::string> &map) const {
  return shared_from_this();
}

Ref TypeId::prefix_ids(const std::set<std::string> &bindings,
                       const std::string &pre) const {
  if (in(id.name, bindings)) {
    return type_id(prefix(bindings, pre, id));
  } else {
    return shared_from_this();
  }
}

Location TypeId::get_location() const {
  return id.location;
}

TypeVariable::TypeVariable(Identifier id) : id(id) {
  for (auto ch : id.name) {
    assert(islower(ch) || !isalpha(ch));
  }
}

TypeVariable::TypeVariable(Location location) : id(gensym(location)) {
  for (auto ch : id.name) {
    assert(islower(ch) || !isalpha(ch));
  }
}

std::ostream &TypeVariable::emit(std::ostream &os,
                                 const Map &bindings,
                                 int parent_precedence) const {
  auto instance_iter = bindings.find(id.name);
  if (instance_iter != bindings.end()) {
    assert(instance_iter->second != shared_from_this());
    return instance_iter->second->emit(os, bindings, parent_precedence);
  } else {
    return os << string_format("%s", id.name.c_str());
  }
}

void TypeVariable::compute_ftvs() const {
  ftvs_.insert(id.name);
}

Ref TypeVariable::eval(const TypeEnv &type_env) const {
  return shared_from_this();
}

Ref TypeVariable::rebind(const Map &bindings) const {
  return get(bindings, id.name, shared_from_this());
}

Ref TypeVariable::remap_vars(
    const std::map<std::string, std::string> &map) const {
  auto iter = map.find(id.name);
  if (iter != map.end()) {
    return type_variable(Identifier{iter->second, id.location});
  } else {
    return shared_from_this();
  }
}

Ref TypeVariable::prefix_ids(const std::set<std::string> &bindings,
                             const std::string &pre) const {
  return shared_from_this();
}

Location TypeVariable::get_location() const {
  return id.location;
}

TypeOperator::TypeOperator(Ref oper, Ref operand)
    : oper(oper), operand(operand) {
}

std::ostream &TypeOperator::emit(std::ostream &os,
                                 const Map &bindings,
                                 int parent_precedence) const {
  if (is_type_id(oper->rebind(bindings), VECTOR_TYPE)) {
    os << "[";
    operand->emit(os, bindings, 0);
    return os << "]";
  } else {
    Parens parens(os, parent_precedence, get_precedence());
    auto rebound_oper = oper->rebind(bindings);
    if (auto op = dyncast<const TypeOperator>(rebound_oper)) {
      if (auto inner_op = dyncast<const TypeId>(op->oper)) {
        if (strspn(inner_op->id.name.c_str(), MATHY_SYMBOLS) ==
            inner_op->id.name.size()) {
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

void TypeOperator::compute_ftvs() const {
  set_concat(ftvs_, oper->get_ftvs());
  set_concat(ftvs_, operand->get_ftvs());
}

Ref TypeOperator::eval(const TypeEnv &type_env) const {
  if (type_env.size() == 0) {
    return shared_from_this();
  }

  auto new_oper = oper->eval(type_env);
  if (new_oper != oper) {
    return new_oper->apply(operand->eval(type_env));
  }
  return shared_from_this();
}

Ref TypeOperator::rebind(const Map &bindings) const {
  if (bindings.size() == 0) {
    return shared_from_this();
  }

  return ::type_operator(oper->rebind(bindings), operand->rebind(bindings));
}

Ref TypeOperator::remap_vars(
    const std::map<std::string, std::string> &map) const {
  return ::type_operator(oper->remap_vars(map), operand->remap_vars(map));
}

Ref TypeOperator::prefix_ids(const std::set<std::string> &bindings,
                             const std::string &pre) const {
  return ::type_operator(oper->prefix_ids(bindings, pre),
                         operand->prefix_ids(bindings, pre));
}

Location TypeOperator::get_location() const {
  return oper->get_location();
}

TypeTuple::TypeTuple(Location location, const Refs &dimensions)
    : location(location), dimensions(dimensions) {
#ifdef ZION_DEBUG
  for (auto dimension : dimensions) {
    assert(dimension != nullptr);
  }
#endif
}

std::ostream &TypeTuple::emit(std::ostream &os,
                              const Map &bindings,
                              int parent_precedence) const {
  os << "(";
  join_dimensions(os, dimensions, {}, bindings);
  if (dimensions.size() != 0) {
    os << ",";
  }
  return os << ")";
}

void TypeTuple::compute_ftvs() const {
  for (auto &dimension : dimensions) {
    set_concat(ftvs_, dimension->get_ftvs());
  }
}

Ref TypeTuple::eval(const TypeEnv &type_env) const {
  if (type_env.size() == 0) {
    return shared_from_this();
  }

  bool anything_affected = false;
  Refs type_dimensions;
  for (auto dimension : dimensions) {
    auto new_dim = dimension->eval(type_env);
    if (new_dim != dimension) {
      anything_affected = true;
    }
    type_dimensions.push_back(new_dim);
  }

  if (anything_affected) {
    return ::type_tuple(type_dimensions);
  } else {
    return shared_from_this();
  }
}

Ref TypeTuple::rebind(const Map &bindings) const {
  if (bindings.size() == 0) {
    return shared_from_this();
  }

  bool anything_was_rebound = false;
  Refs type_dimensions;
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

Ref TypeTuple::remap_vars(const std::map<std::string, std::string> &map) const {
  bool anything_was_rebound = false;
  Refs type_dimensions;
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

Ref TypeTuple::prefix_ids(const std::set<std::string> &bindings,
                          const std::string &pre) const {
  bool anything_was_rebound = false;
  Refs type_dimensions;
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

Location TypeTuple::get_location() const {
  return location;
}

TypeLambda::TypeLambda(Identifier binding, Ref body)
    : binding(binding), body(body) {
  assert(islower(binding.name[0]));
}

std::ostream &TypeLambda::emit(std::ostream &os,
                               const Map &bindings_,
                               int parent_precedence) const {
  Parens parens(os, parent_precedence, get_precedence());

  auto var_name = binding.name;
  auto new_name = gensym(get_location());
  os << "Λ " << new_name.name << " . ";
  Map bindings = bindings_;
  bindings[var_name] = type_id(new_name);
  body->emit(os, bindings, get_precedence());
  return os;
}

void TypeLambda::compute_ftvs() const {
  assert(false);
#if 0
		Map bindings;
		bindings[binding.name] = type_bottom();
		return body->rebind(bindings)->get_predicate_map();
#endif
}

Ref TypeLambda::rebind(const Map &bindings_) const {
  if (bindings_.size() == 0) {
    return shared_from_this();
  }

  Map bindings = bindings_;
  auto binding_iter = bindings.find(binding.name);
  if (binding_iter != bindings.end()) {
    bindings.erase(binding_iter);
  }
  return ::type_lambda(binding, body->rebind(bindings));
}

Ref TypeLambda::eval(const TypeEnv &type_env) const {
  auto new_body = body->eval(type_env);
  if (new_body != body) {
    return ::type_lambda(binding, new_body);
  } else {
    return shared_from_this();
  }
}

Ref TypeLambda::remap_vars(
    const std::map<std::string, std::string> &map_) const {
  assert(false);
  if (in(binding.name, map_)) {
    std::map<std::string, std::string> map = map_;
    auto new_binding = alphabetize(map.size());
    map[binding.name] = new_binding;
    assert(!in(new_binding, map_));
    assert(!in(new_binding, get_ftvs()));
    return ::type_lambda(Identifier{new_binding, binding.location},
                         body->remap_vars(map));
  }
  return ::type_lambda(binding, body->remap_vars(map_));
}

Ref TypeLambda::prefix_ids(const std::set<std::string> &bindings,
                           const std::string &pre) const {
  return type_lambda(binding,
                     body->prefix_ids(without(bindings, binding.name), pre));
}

Ref TypeLambda::apply(types::Ref type) const {
  Map bindings;
  bindings[binding.name] = type;
  return body->rebind(bindings);
}

Location TypeLambda::get_location() const {
  return binding.location;
}

bool is_unit(Ref type) {
  if (auto tuple = dyncast<const types::TypeTuple>(type)) {
    return tuple->dimensions.size() == 0;
  } else {
    return false;
  }
}

bool is_type_id(Ref type, const std::string &type_name) {
  if (auto pti = dyncast<const types::TypeId>(type)) {
    return pti->id.name == type_name;
  }

  return false;
}

Refs rebind(const Refs &types, const Map &bindings) {
  Refs rebound_types;
  for (const auto &type : types) {
    rebound_types.push_back(type->rebind(bindings));
  }
  return rebound_types;
}

Ref unitize(Ref type) {
  Map bindings;
  for (auto &ftv : type->get_ftvs()) {
    bindings[ftv] = type_unit(INTERNAL_LOC());
  }
  return type->rebind(bindings);
}

bool is_callable(const Ref &t) {
  auto op = dyncast<const types::TypeOperator>(t);
  if (op != nullptr) {
    auto nested_op = dyncast<const types::TypeOperator>(op->oper);
    if (nested_op != nullptr) {
      return is_type_id(nested_op->oper, ARROW_TYPE_OPERATOR);
    }
  }
  return false;
}

} // namespace types

types::Ref type_id(Identifier id) {
  return std::make_shared<types::TypeId>(id);
}

types::Ref type_variable(const Identifier &id) {
  return std::make_shared<types::TypeVariable>(id);
}

types::Ref type_variable(Location location) {
  return std::make_shared<types::TypeVariable>(location);
}

types::Refs type_variables(const Identifiers &ids) {
  types::Refs types;
  types.reserve(ids.size());
  for (auto &id : ids) {
    types.push_back(type_variable(id));
  }
  return types;
}

types::Ref type_unit(Location location) {
  return std::make_shared<types::TypeTuple>(location, types::Refs{});
}

types::Ref type_bottom() {
  static auto bottom_type = std::make_shared<types::TypeId>(
      make_iid(BOTTOM_TYPE));
  return bottom_type;
}

types::Ref type_bool(Location location) {
  return std::make_shared<types::TypeId>(Identifier{BOOL_TYPE, location});
}

types::Ref type_vector_type(types::Ref element) {
  return type_operator(
      type_id(Identifier{VECTOR_TYPE, element->get_location()}), element);
}

types::Ref type_string(Location location) {
  return type_id(Identifier{STRING_TYPE, location});
}

types::Ref type_int(Location location) {
  return std::make_shared<types::TypeId>(Identifier{INT_TYPE, location});
}

types::Ref type_null(Location location) {
  return std::make_shared<types::TypeId>(Identifier{NULL_TYPE, location});
}

types::Ref type_void(Location location) {
  return std::make_shared<types::TypeId>(Identifier{VOID_TYPE, location});
}

types::Ref type_operator(types::Ref operator_, types::Ref operand) {
  return std::make_shared<types::TypeOperator>(operator_, operand);
}

types::Ref type_operator(const types::Refs &xs) {
  assert(xs.size() >= 2);
  types::Ref result = type_operator(xs[0], xs[1]);
  for (int i = 2; i < xs.size(); ++i) {
    result = type_operator(result, xs[i]);
  }
  return result;
}

types::Scheme::Ref scheme(std::vector<std::string> vars,
                          const types::ClassPredicates &predicates,
                          const types::Ref &type) {
  return std::make_shared<types::Scheme>(vars, predicates, type);
}

types::NameIndex get_name_index_from_ids(Identifiers ids) {
  types::NameIndex name_index;
  int i = 0;
  for (auto id : ids) {
    name_index[id.name] = i++;
  }
  return name_index;
}

types::Ref type_map(types::Ref a, types::Ref b) {
  return type_operator(
      type_operator(type_id(Identifier{"Map", a->get_location()}), a), b);
}

types::TypeTuple::Ref type_tuple(types::Refs dimensions) {
  assert(dimensions.size() != 0);
  return type_tuple(dimensions[0]->get_location(), dimensions);
}

types::TypeTuple::Ref type_tuple(Location location, types::Refs dimensions) {
  return std::make_shared<types::TypeTuple>(location, dimensions);
}

types::Ref type_arrow(types::Ref a, types::Ref b) {
  return type_arrow(a->get_location(), a, b);
}

types::Ref type_arrow(Location location, types::Ref a, types::Ref b) {
  return type_operator(
      type_operator(type_id(Identifier{ARROW_TYPE_OPERATOR, location}), a), b);
}

types::Ref type_arrows(types::Refs types, int offset) {
  assert(int(types.size()) - offset > 0);
  if (types.size() - offset == 1) {
    return types[offset];
  } else {
    return type_arrow(types[offset]->get_location(), types[offset],
                      type_arrows(types, offset + 1));
  }
}

types::Ref type_ptr(types::Ref raw) {
  return type_operator(
      type_id(Identifier{PTR_TYPE_OPERATOR, raw->get_location()}), raw);
}

types::Ref type_lambda(Identifier binding, types::Ref body) {
  return std::make_shared<types::TypeLambda>(binding, body);
}

types::Ref type_tuple_accessor(int i,
                               int max,
                               const std::vector<std::string> &vars) {
  types::Refs dims;
  for (int j = 0; j < max; ++j) {
    dims.push_back(type_variable(make_iid(vars[j])));
  }
  return type_arrows({type_tuple(dims), type_variable(make_iid(vars[i]))});
}

std::ostream &operator<<(std::ostream &os, const types::Ref &type) {
  os << type->str();
  return os;
}

std::string str(types::Refs Refs) {
  std::stringstream ss;
  ss << "(";
  const char *sep = "";
  for (auto p : Refs) {
    ss << sep << p->str();
    sep = ", ";
  }
  ss << ")";
  return ss.str();
}

std::string str(const types::Map &coll) {
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

std::string str(const DataCtorsMap &data_ctors_map) {
  std::stringstream ss;
  const char *delim = "";
  for (auto pair : data_ctors_map) {
    ss << delim << pair.first << ": " << ::str(pair.second);
    delim = ", ";
  }
  return ss.str();
}

std::string str(const types::Ftvs &ftvs) {
  std::stringstream ss;
  ss << "{";
  ss << join_with(ftvs, ", ",
                  [](const std::string &s) { return "C_TYPE" + s + C_RESET; });
  ss << "}";
  return ss.str();
}

std::ostream &join_dimensions(std::ostream &os,
                              const types::Refs &dimensions,
                              const types::NameIndex &name_index,
                              const types::Map &bindings) {
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

void unfold_binops_rassoc(std::string id,
                          types::Ref t,
                          types::Refs &unfolding) {
  auto op = dyncast<const types::TypeOperator>(t);
  if (op != nullptr) {
    auto nested_op = dyncast<const types::TypeOperator>(op->oper);
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

void unfold_ops_lassoc(types::Ref t, types::Refs &unfolding) {
  auto op = dyncast<const types::TypeOperator>(t);
  if (op != nullptr) {
    unfold_ops_lassoc(op->oper, unfolding);
    unfolding.push_back(op->operand);
  } else {
    unfolding.push_back(t);
  }
}

void insert_needed_defn(NeededDefns &needed_defns,
                        const DefnId &defn_id,
                        Location location,
                        const DefnId &for_defn_id) {
  needed_defns[defn_id.unitize()].push_back({location, for_defn_id.unitize()});
}

types::Ref type_deref(Location location, types::Ref type) {
  auto ptr = safe_dyncast<const types::TypeOperator>(type);
  if (types::is_type_id(ptr->oper, PTR_TYPE_OPERATOR)) {
    return ptr->operand;
  } else {
    throw user_error(location, "attempt to dereference value of type %s",
                     type->str().c_str());
  }
}

types::Ref tuple_deref_type(Location location,
                            const types::Ref &tuple_,
                            int index) {
  auto tuple = safe_dyncast<const types::TypeTuple>(tuple_);
  if (tuple->dimensions.size() < index || index < 0) {
    auto error = user_error(
        location,
        "attempt to access type of element at index %d which is out of range",
        index);
    error.add_info(tuple_->get_location(), "type is %s", tuple_->str().c_str());
    throw error;
  }
  return tuple->dimensions[index];
}
