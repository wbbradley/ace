#include "types.h"

#include <iostream>
#include <sstream>

#include "ast.h"
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

void reset_generics() {
  next_generic = 1;
}

Identifier gensym(Location location) {
  /* generate fresh "any" variables */
  return Identifier{
      string_format("__%s", alphabetize(next_generic++).c_str()).c_str(),
      location};
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

void mutating_merge(const types::predicate_map_t::value_type &pair,
                    types::predicate_map_t &c) {
  if (!in(pair.first, c)) {
    c.insert(pair);
  } else {
    for (auto predicate : pair.second) {
      c[pair.first].insert(predicate);
    }
  }
}
void mutating_merge(const types::predicate_map_t &a,
                    types::predicate_map_t &c) {
  for (auto pair : a) {
    mutating_merge(pair, c);
  }
}

types::predicate_map_t merge(const types::predicate_map_t &a,
                             const types::predicate_map_t &b) {
  types::predicate_map_t c;
  mutating_merge(a, c);
  mutating_merge(b, c);
  return c;
}

types::predicate_map_t safe_merge(const types::predicate_map_t &a,
                                  const types::predicate_map_t &b) {
  types::predicate_map_t c;
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

const predicate_map_t &Type::get_predicate_map() const {
  /* maintain this object's predicate map cache */
  if (!predicate_map_valid) {
    compute_predicate_map();
    predicate_map_valid = true;
  }
  return pm_;
}

std::string Type::str() const {
  return str(map{});
}

std::string Type::str(const map &bindings) const {
  return string_format(c_type("%s"), this->repr(bindings).c_str());
}

std::string Type::repr(const map &bindings) const {
  std::stringstream ss;
  emit(ss, bindings, 0);
  return ss.str();
}

std::shared_ptr<Scheme> Type::generalize(
    const types::predicate_map_t &pm) const {
  std::vector<std::string> vs;
  predicate_map_t new_pm;
  Type::map bindings;
  for (auto &ftv : get_predicate_map()) {
    if (!in(ftv.first, pm)) {
      vs.push_back(ftv.first);
      mutating_merge(ftv, new_pm);
      bindings[ftv.first] = type_variable(make_iid(ftv.first));
    }
  }
  return scheme(vs, new_pm, rebind(bindings));
}

Type::ref Type::apply(types::Type::ref type) const {
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
                           const map &bindings,
                           int parent_precedence) const {
  return os << id.name;
}

int TypeId::ftv_count() const {
  /* how many free type variables exist in this type? */
  return 0;
}

void TypeId::compute_predicate_map() const {
}

Type::ref TypeId::eval(const type_env_t &type_env) const {
  debug_above(5, log("trying to get %s from type_env {%s}", id.name.c_str(),
                     ::str(type_env).c_str()));
  return get(type_env, id.name, shared_from_this());
}

Type::ref TypeId::rebind(const map &bindings) const {
  return shared_from_this();
}

Type::ref TypeId::remap_vars(
    const std::map<std::string, std::string> &map) const {
  return shared_from_this();
}

Type::ref TypeId::prefix_ids(const std::set<std::string> &bindings,
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

TypeVariable::TypeVariable(Identifier id, std::set<std::string> predicates)
    : id(id), predicates(predicates) {
  for (auto ch : id.name) {
    assert(islower(ch) || !isalpha(ch));
  }
}

TypeVariable::TypeVariable(Identifier id) : TypeVariable(id, {}) {
}

TypeVariable::TypeVariable(Location location) : id(gensym(location)) {
  for (auto ch : id.name) {
    assert(islower(ch) || !isalpha(ch));
  }
}

std::ostream &TypeVariable::emit(std::ostream &os,
                                 const map &bindings,
                                 int parent_precedence) const {
  auto instance_iter = bindings.find(id.name);
  if (instance_iter != bindings.end()) {
    assert(instance_iter->second != shared_from_this());
    return instance_iter->second->emit(os, bindings, parent_precedence);
  } else {
    if (predicates.size() == 0) {
      return os << string_format("%s", id.name.c_str());
    } else {
      return os << string_format("%s|[%s]", id.name.c_str(),
                                 join(predicates, ", ").c_str());
    }
  }
}

/* how many free type variables exist in this type? */
int TypeVariable::ftv_count() const {
  return 1;
}

void TypeVariable::compute_predicate_map() const {
  pm_[id.name] = predicates;
}

Type::ref TypeVariable::eval(const type_env_t &type_env) const {
  return shared_from_this();
}

Type::ref TypeVariable::rebind(const map &bindings) const {
  return get(bindings, id.name, shared_from_this());
}

Type::ref TypeVariable::remap_vars(
    const std::map<std::string, std::string> &map) const {
  auto iter = map.find(id.name);
  assert(iter != map.end());
  return type_variable(Identifier{iter->second, id.location});
}

Type::ref TypeVariable::prefix_ids(const std::set<std::string> &bindings,
                                   const std::string &pre) const {
  return type_variable(id, prefix(bindings, pre, predicates));
}

Location TypeVariable::get_location() const {
  return id.location;
}

TypeOperator::TypeOperator(Type::ref oper, Type::ref operand)
    : oper(oper), operand(operand) {
}

std::ostream &TypeOperator::emit(std::ostream &os,
                                 const map &bindings,
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

int TypeOperator::ftv_count() const {
  return oper->ftv_count() + operand->ftv_count();
}

void TypeOperator::compute_predicate_map() const {
  mutating_merge(oper->get_predicate_map(), pm_);
  mutating_merge(operand->get_predicate_map(), pm_);
}

Type::ref TypeOperator::eval(const type_env_t &type_env) const {
  if (type_env.size() == 0) {
    return shared_from_this();
  }

  auto new_oper = oper->eval(type_env);
  if (new_oper != oper) {
    return new_oper->apply(operand->eval(type_env));
  }
  return shared_from_this();
}

Type::ref TypeOperator::rebind(const map &bindings) const {
  if (bindings.size() == 0) {
    return shared_from_this();
  }

  return ::type_operator(oper->rebind(bindings), operand->rebind(bindings));
}

Type::ref TypeOperator::remap_vars(
    const std::map<std::string, std::string> &map) const {
  return ::type_operator(oper->remap_vars(map), operand->remap_vars(map));
}

Type::ref TypeOperator::prefix_ids(const std::set<std::string> &bindings,
                                   const std::string &pre) const {
  return ::type_operator(oper->prefix_ids(bindings, pre),
                         operand->prefix_ids(bindings, pre));
}

Location TypeOperator::get_location() const {
  return oper->get_location();
}

TypeTuple::TypeTuple(Location location, const Type::refs &dimensions)
    : location(location), dimensions(dimensions) {
#ifdef ZION_DEBUG
  for (auto dimension : dimensions) {
    assert(dimension != nullptr);
  }
#endif
}

std::ostream &TypeTuple::emit(std::ostream &os,
                              const map &bindings,
                              int parent_precedence) const {
  os << "(";
  join_dimensions(os, dimensions, {}, bindings);
  if (dimensions.size() != 0) {
    os << ",";
  }
  return os << ")";
}

int TypeTuple::ftv_count() const {
  int ftv_sum = 0;
  for (auto dimension : dimensions) {
    ftv_sum += dimension->ftv_count();
  }
  return ftv_sum;
}

void TypeTuple::compute_predicate_map() const {
  for (auto dimension : dimensions) {
    mutating_merge(dimension->get_predicate_map(), pm_);
  }
}

Type::ref TypeTuple::eval(const type_env_t &type_env) const {
  if (type_env.size() == 0) {
    return shared_from_this();
  }

  bool anything_affected = false;
  refs type_dimensions;
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

Type::ref TypeTuple::rebind(const map &bindings) const {
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

Type::ref TypeTuple::remap_vars(
    const std::map<std::string, std::string> &map) const {
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

Type::ref TypeTuple::prefix_ids(const std::set<std::string> &bindings,
                                const std::string &pre) const {
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

Location TypeTuple::get_location() const {
  return location;
}

TypeLambda::TypeLambda(Identifier binding, Type::ref body)
    : binding(binding), body(body) {
  assert(islower(binding.name[0]));
}

std::ostream &TypeLambda::emit(std::ostream &os,
                               const map &bindings_,
                               int parent_precedence) const {
  Parens parens(os, parent_precedence, get_precedence());

  auto var_name = binding.name;
  auto new_name = gensym(get_location());
  os << "Λ " << new_name.name << " . ";
  map bindings = bindings_;
  bindings[var_name] = type_id(new_name);
  body->emit(os, bindings, get_precedence());
  return os;
}

int TypeLambda::ftv_count() const {
  /* pretend this is getting applied */
  map bindings;
  bindings[binding.name] = type_bottom();
  return body->rebind(bindings)->ftv_count();
}

void TypeLambda::compute_predicate_map() const {
  assert(false);
#if 0
		map bindings;
		bindings[binding.name] = type_bottom();
		return body->rebind(bindings)->get_predicate_map();
#endif
}

Type::ref TypeLambda::rebind(const map &bindings_) const {
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

Type::ref TypeLambda::eval(const type_env_t &type_env) const {
  auto new_body = body->eval(type_env);
  if (new_body != body) {
    return ::type_lambda(binding, new_body);
  } else {
    return shared_from_this();
  }
}

Type::ref TypeLambda::remap_vars(
    const std::map<std::string, std::string> &map_) const {
  assert(false);
  if (in(binding.name, map_)) {
    std::map<std::string, std::string> map = map_;
    auto new_binding = alphabetize(map.size());
    map[binding.name] = new_binding;
    assert(!in(new_binding, map_));
    assert(!in(new_binding, get_predicate_map()));
    return ::type_lambda(Identifier{new_binding, binding.location},
                         body->remap_vars(map));
  }
  return ::type_lambda(binding, body->remap_vars(map_));
}

Type::ref TypeLambda::prefix_ids(const std::set<std::string> &bindings,
                                 const std::string &pre) const {
  return type_lambda(binding,
                     body->prefix_ids(without(bindings, binding.name), pre));
}

Type::ref TypeLambda::apply(types::Type::ref type) const {
  map bindings;
  bindings[binding.name] = type;
  return body->rebind(bindings);
}

Location TypeLambda::get_location() const {
  return binding.location;
}

bool is_unit(Type::ref type) {
  if (auto tuple = dyncast<const types::TypeTuple>(type)) {
    return tuple->dimensions.size() == 0;
  } else {
    return false;
  }
}

bool is_type_id(Type::ref type, const std::string &type_name) {
  if (auto pti = dyncast<const types::TypeId>(type)) {
    return pti->id.name == type_name;
  }

  return false;
}

Type::refs rebind(const Type::refs &types, const Type::map &bindings) {
  Type::refs rebound_types;
  for (const auto &type : types) {
    rebound_types.push_back(type->rebind(bindings));
  }
  return rebound_types;
}

types::Type::ref Scheme::instantiate(Location location) {
  Type::map subst;
  for (auto var : vars) {
    subst[var] = type_variable(gensym(location), predicates.count(var)
                                                     ? predicates.at(var)
                                                     : std::set<std::string>{});
  }
  return type->rebind(subst);
}

Type::map remove_bindings(const Type::map &env,
                          const std::vector<std::string> &vars) {
  Type::map new_map{env};
  for (auto var : vars) {
    new_map.erase(var);
  }
  return new_map;
}

Scheme::ref Scheme::rebind(const types::Type::map &bindings) {
  /* this is subtle because it actually rebinds type variables that are free
   * within the not-yet-normalized scheme. This is because the map containing
   * the schemes is a working set of types that are waiting to be bound. In some
   * cases the variability of the inner types can be resolved. */
  return scheme(vars, predicates,
                type->rebind(remove_bindings(bindings, vars)));
}

Scheme::ref Scheme::normalize() {
  std::map<std::string, std::string> ord;
  predicate_map_t pm;

  int counter = 0;
  for (auto &ftv : type->get_predicate_map()) {
    auto new_name = alphabetize(counter++);
    ord[ftv.first] = new_name;
    if (in(ftv.first, predicates)) {
      assert(predicates.count(ftv.first));
      pm[new_name] = predicates.at(ftv.first);
    }
  }
  return scheme(values(ord), pm, type->remap_vars(ord));
}

std::string Scheme::str() const {
  return repr();
}

std::string Scheme::repr() const {
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

int Scheme::btvs() const {
  int sum = 0;
  for (auto pair : predicates) {
    sum += pair.second.size();
  }
  return sum;
}

const predicate_map_t &Scheme::get_predicate_map() {
  if (predicate_map_valid) {
    return pm_;
  } else {
    pm_ = type->get_predicate_map();
    for (auto var : vars) {
      pm_.erase(var);
    }
    predicate_map_valid = true;
    return pm_;
  }
}

Location Scheme::get_location() const {
  return type->get_location();
}

Type::ref unitize(Type::ref type) {
  Type::map bindings;
  for (auto &pair : type->get_predicate_map()) {
    bindings[pair.first] = type_unit(INTERNAL_LOC());
    debug_above(6, log("assigning %s binding for [%s] to %s",
                       pair.first.c_str(), join(pair.second).c_str(),
                       bindings.at(pair.first)->str().c_str()));
    assert(pair.second.size() == 0);
  }
  return type->rebind(bindings);
}

bool is_callable(const Type::ref &t) {
  auto op = dyncast<const types::TypeOperator>(t);
  if (op != nullptr) {
    auto nested_op = dyncast<const types::TypeOperator>(op->oper);
    if (nested_op != nullptr) {
      return is_type_id(nested_op->oper, ARROW_TYPE_OPERATOR);
    }
  }
  return false;
}

std::unordered_set<std::string> get_ftvs(const types::Type::ref &type) {
  std::unordered_set<std::string> ftvs;
  for (auto &pair : type->get_predicate_map()) {
    ftvs.insert(pair.first);
  }
  return ftvs;
}

} // namespace types

types::Type::ref type_id(Identifier id) {
  return std::make_shared<types::TypeId>(id);
}

types::Type::ref type_variable(Identifier id) {
  return std::make_shared<types::TypeVariable>(id);
}

types::Type::ref type_variable(Identifier id,
                               const std::set<std::string> &predicates) {
  return std::make_shared<types::TypeVariable>(id, predicates);
}

types::Type::ref type_variable(Location location) {
  return std::make_shared<types::TypeVariable>(location);
}

types::Type::ref type_unit(Location location) {
  return std::make_shared<types::TypeTuple>(location, types::Type::refs{});
}

types::Type::ref type_bottom() {
  static auto bottom_type = std::make_shared<types::TypeId>(
      make_iid(BOTTOM_TYPE));
  return bottom_type;
}

types::Type::ref type_bool(Location location) {
  return std::make_shared<types::TypeId>(Identifier{BOOL_TYPE, location});
}

types::Type::ref type_vector_type(types::Type::ref element) {
  return type_operator(
      type_id(Identifier{VECTOR_TYPE, element->get_location()}), element);
}

types::Type::ref type_string(Location location) {
  return type_id(Identifier{STRING_TYPE, location});
}

types::Type::ref type_int(Location location) {
  return std::make_shared<types::TypeId>(Identifier{INT_TYPE, location});
}

types::Type::ref type_null(Location location) {
  return std::make_shared<types::TypeId>(Identifier{NULL_TYPE, location});
}

types::Type::ref type_void(Location location) {
  return std::make_shared<types::TypeId>(Identifier{VOID_TYPE, location});
}

types::Type::ref type_operator(types::Type::ref operator_,
                               types::Type::ref operand) {
  return std::make_shared<types::TypeOperator>(operator_, operand);
}

types::Type::ref type_operator(const types::Type::refs &xs) {
  assert(xs.size() >= 2);
  types::Type::ref result = type_operator(xs[0], xs[1]);
  for (int i = 2; i < xs.size(); ++i) {
    result = type_operator(result, xs[i]);
  }
  return result;
}

types::Scheme::ref scheme(std::vector<std::string> vars,
                          const types::predicate_map_t &predicates,
                          types::Type::ref type) {
#if 0
  if (type->str().find("|") != std::string::npos) {
    log_location(type->get_location(),
                 "found predicates in %s when calling scheme({%s}, {%s}, %s)",
                 type->str().c_str(), join(vars).c_str(),
                 str(predicates).c_str(), type->str().c_str());
    dbg();
  }
#endif
  return std::make_shared<types::Scheme>(vars, predicates, type);
}

types::name_index_t get_name_index_from_ids(identifiers_t ids) {
  types::name_index_t name_index;
  int i = 0;
  for (auto id : ids) {
    name_index[id.name] = i++;
  }
  return name_index;
}

types::Type::ref type_map(types::Type::ref a, types::Type::ref b) {
  return type_operator(
      type_operator(type_id(Identifier{"Map", a->get_location()}), a), b);
}

types::TypeTuple::ref type_tuple(types::Type::refs dimensions) {
  assert(dimensions.size() != 0);
  return type_tuple(dimensions[0]->get_location(), dimensions);
}

types::TypeTuple::ref type_tuple(Location location,
                                 types::Type::refs dimensions) {
  return std::make_shared<types::TypeTuple>(location, dimensions);
}

types::Type::ref type_arrow(types::Type::ref a, types::Type::ref b) {
  return type_arrow(a->get_location(), a, b);
}

types::Type::ref type_arrow(Location location,
                            types::Type::ref a,
                            types::Type::ref b) {
  return type_operator(
      type_operator(type_id(Identifier{ARROW_TYPE_OPERATOR, location}), a), b);
}

types::Type::ref type_arrows(types::Type::refs types, int offset) {
  assert(int(types.size()) - offset > 0);
  if (types.size() - offset == 1) {
    return types[offset];
  } else {
    return type_arrow(types[offset]->get_location(), types[offset],
                      type_arrows(types, offset + 1));
  }
}

types::Type::ref type_ptr(types::Type::ref raw) {
  return type_operator(
      type_id(Identifier{PTR_TYPE_OPERATOR, raw->get_location()}), raw);
}

types::Type::ref type_lambda(Identifier binding, types::Type::ref body) {
  return std::make_shared<types::TypeLambda>(binding, body);
}

types::Type::ref type_tuple_accessor(int i,
                                     int max,
                                     const std::vector<std::string> &vars) {
  types::Type::refs dims;
  for (int j = 0; j < max; ++j) {
    dims.push_back(type_variable(make_iid(vars[j])));
  }
  return type_arrows({type_tuple(dims), type_variable(make_iid(vars[i]))});
}

std::ostream &operator<<(std::ostream &os, const types::Type::ref &type) {
  os << type->str();
  return os;
}

std::string str(types::Type::refs refs) {
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

std::string str(const types::Type::map &coll) {
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

std::string str(const types::predicate_map_t &pm) {
  bool saw_predicate = false;
  std::stringstream ss;
  const char *delim = " [where ";
  for (auto pair : pm) {
    for (auto predicate : pair.second) {
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

std::ostream &join_dimensions(std::ostream &os,
                              const types::Type::refs &dimensions,
                              const types::name_index_t &name_index,
                              const types::Type::map &bindings) {
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
                          types::Type::ref t,
                          types::Type::refs &unfolding) {
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

void unfold_ops_lassoc(types::Type::ref t, types::Type::refs &unfolding) {
  auto op = dyncast<const types::TypeOperator>(t);
  if (op != nullptr) {
    unfold_ops_lassoc(op->oper, unfolding);
    unfolding.push_back(op->operand);
  } else {
    unfolding.push_back(t);
  }
}

void insert_needed_defn(needed_defns_t &needed_defns,
                        const DefnId &defn_id,
                        Location location,
                        const DefnId &for_defn_id) {
  needed_defns[defn_id.unitize()].push_back({location, for_defn_id.unitize()});
}

types::Type::ref type_deref(Location location, types::Type::ref type) {
  auto ptr = safe_dyncast<const types::TypeOperator>(type);
  if (types::is_type_id(ptr->oper, PTR_TYPE_OPERATOR)) {
    return ptr->operand;
  } else {
    throw user_error(location, "attempt to dereference value of type %s",
                     type->str().c_str());
  }
}

types::Type::ref tuple_deref_type(Location location,
                                  const types::Type::ref &tuple_,
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
