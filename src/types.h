#pragma once

#include <memory>
#include <unordered_set>

#include "ast_decls.h"
#include "defn_id.h"
#include "identifier.h"
#include "utils.h"
#include "zion.h"

extern const char *NULL_TYPE;
extern const char *STD_MAP_TYPE;
extern const char *VOID_TYPE;
extern const char *BOTTOM_TYPE;

/* used to reset the generic type id counter */
void reset_generics();

namespace types {

struct Type;

typedef std::map<std::string, int> name_index_t;
typedef std::map<std::string, std::set<std::string>> predicate_map_t;
typedef std::map<std::string, std::shared_ptr<const Type>> type_env_t;

struct signature;
struct Scheme;

struct Type : public std::enable_shared_from_this<Type> {
  typedef std::shared_ptr<const Type> ref;
  typedef std::vector<ref> refs;
  typedef std::map<std::string, ref> map;
  typedef std::pair<ref, ref> pair;

  virtual ~Type() {
  }

  virtual std::ostream &emit(std::ostream &os,
                             const map &bindings,
                             int parent_precedence) const = 0;

  /* how many free type variables exist in this type? NB: Assumes you have
   * already bound existing bindings at the callsite prior to this check. */
  virtual int ftv_count() const = 0;

  /* NB: Also assumes you have rebound the bindings at the callsite. */
  const predicate_map_t &get_predicate_map() const;
  virtual void compute_predicate_map() const = 0;

  virtual Type::ref eval(const type_env_t &type_env) const = 0;
  std::shared_ptr<Scheme> generalize(const types::predicate_map_t &pm) const;
  std::string repr(const map &bindings) const;
  std::string repr() const {
    return this->repr({});
  }

  virtual Location get_location() const = 0;

  std::string str() const;
  std::string str(const map &bindings) const;
  std::string get_signature() const {
    return repr();
  }

  virtual Type::ref rebind(const map &bindings) const = 0;
  virtual Type::ref remap_vars(
      const std::map<std::string, std::string> &map) const = 0;
  virtual Type::ref prefix_ids(const std::set<std::string> &bindings,
                               const std::string &pre) const = 0;
  virtual Type::ref apply(ref type) const;

  virtual int get_precedence() const {
    return 10;
  }

protected:
  mutable bool predicate_map_valid = false;
  mutable predicate_map_t pm_;
};

struct CompareType {
  bool operator()(const Type::ref &a, const Type::ref &b) const {
    return a->repr() < b->repr();
  }
};

struct TypeVariable final : public Type {
  TypeVariable(Identifier id, std::set<std::string> predicates);
  TypeVariable(Identifier id);
  TypeVariable(Location location /* auto-generated fresh type variables */);
  Identifier id;
  std::set<std::string> predicates;

  std::ostream &emit(std::ostream &os,
                     const map &bindings,
                     int parent_precedence) const override;
  int ftv_count() const override;
  void compute_predicate_map() const override;
  Type::ref eval(const type_env_t &type_env) const override;
  Type::ref rebind(const map &bindings) const override;
  Type::ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  Type::ref prefix_ids(const std::set<std::string> &bindings,
                       const std::string &pre) const override;
  Location get_location() const override;
};

struct TypeId final : public Type {
  TypeId(Identifier id);
  Identifier id;

  std::ostream &emit(std::ostream &os,
                     const map &bindings,
                     int parent_precedence) const override;
  int ftv_count() const override;
  void compute_predicate_map() const override;
  Type::ref eval(const type_env_t &type_env) const override;
  Type::ref rebind(const map &bindings) const override;
  Type::ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  Type::ref prefix_ids(const std::set<std::string> &bindings,
                       const std::string &pre) const override;
  Location get_location() const override;
};

struct TypeOperator final : public Type {
  typedef std::shared_ptr<const TypeOperator> ref;

  TypeOperator(Type::ref oper, Type::ref operand);
  Type::ref oper;
  Type::ref operand;

  int get_precedence() const override {
    return 7;
  }

  std::ostream &emit(std::ostream &os,
                     const map &bindings,
                     int parent_precedence) const override;
  int ftv_count() const override;
  void compute_predicate_map() const override;
  Type::ref eval(const type_env_t &type_env) const override;
  Type::ref rebind(const map &bindings) const override;
  Type::ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  Type::ref prefix_ids(const std::set<std::string> &bindings,
                       const std::string &pre) const override;
  Location get_location() const override;
};

struct TypeTuple final : public Type {
  typedef std::shared_ptr<const TypeTuple> ref;

  TypeTuple(Location location, const Type::refs &dimensions);

  std::ostream &emit(std::ostream &os,
                     const map &bindings,
                     int parent_precedence) const override;
  int ftv_count() const override;
  void compute_predicate_map() const override;
  Type::ref eval(const type_env_t &type_env) const override;
  Type::ref rebind(const map &bindings) const override;
  Type::ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  Type::ref prefix_ids(const std::set<std::string> &bindings,
                       const std::string &pre) const override;
  Location get_location() const override;

  Location location;
  Type::refs dimensions;
};

struct TypeLambda final : public Type {
  TypeLambda(Identifier binding, Type::ref body);
  Identifier binding;
  Type::ref body;

  int get_precedence() const override {
    return 6;
  }
  std::ostream &emit(std::ostream &os,
                     const map &bindings,
                     int parent_precedence) const override;
  int ftv_count() const override;
  void compute_predicate_map() const override;
  Type::ref eval(const type_env_t &type_env) const override;
  Type::ref rebind(const map &bindings) const override;
  Type::ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  Type::ref prefix_ids(const std::set<std::string> &bindings,
                       const std::string &pre) const override;
  Type::ref apply(types::Type::ref type) const override;
  Location get_location() const override;
};

struct Scheme final : public std::enable_shared_from_this<Scheme> {
  typedef std::shared_ptr<Scheme> ref;
  typedef std::vector<ref> refs;
  typedef std::map<std::string, ref> map;

  Scheme(std::vector<std::string> vars,
         const predicate_map_t &predicates,
         types::Type::ref type)
      : vars(vars), predicates(predicates), type(type) {
  }
  types::Type::ref instantiate(Location location);
  Scheme::ref rebind(const types::Type::map &env);
  Scheme::ref normalize();

  /* count of the bounded type variables */
  int btvs() const;

  const predicate_map_t &get_predicate_map();
  std::string str() const;
  std::string repr() const;
  Location get_location() const;

  std::vector<std::string> const vars;
  predicate_map_t const predicates;
  types::Type::ref const type;

private:
  mutable bool predicate_map_valid = false;
  mutable predicate_map_t pm_;
};

bool is_unit(Type::ref type);
bool is_type_id(Type::ref type, const std::string &type_name);
Type::refs rebind(const Type::refs &types, const Type::map &bindings);
Type::ref unitize(Type::ref type);
bool is_callable(const types::Type::ref &type);
std::unordered_set<std::string> get_ftvs(const types::Type::ref &type);
}; // namespace types

typedef std::map<std::string, types::Type::map> data_ctors_map_t;
typedef std::unordered_map<const bitter::Expr *, types::Type::ref>
    tracked_types_t;
typedef std::unordered_map<std::string, int> ctor_id_map_t;
struct DefnRef {
  Location location;
  DefnId from_defn_id;
};

typedef std::map<DefnId, std::vector<DefnRef>> needed_defns_t;
void insert_needed_defn(needed_defns_t &needed_defns,
                        const DefnId &defn_id,
                        Location location,
                        const DefnId &from_defn_id);

Identifier gensym(Location location);

/* type data ctors */
types::Type::ref type_bottom();
types::Type::ref type_bool(Location location);
types::Type::ref type_bool(Location location);
types::Type::ref type_string(Location location);
types::Type::ref type_int(Location location);
types::Type::ref type_unit(Location location);
types::Type::ref type_null(Location location);
types::Type::ref type_void(Location location);
types::Type::ref type_map(types::Type::ref a, types::Type::ref b);
types::Type::ref type_arrow(Location location,
                            types::Type::ref a,
                            types::Type::ref b);
types::Type::ref type_arrow(types::Type::ref a, types::Type::ref b);
types::Type::ref type_arrows(types::Type::refs types, int offset = 0);
types::Type::ref type_id(Identifier var);
types::Type::ref type_variable(Identifier id,
                               const std::set<std::string> &predicates);
types::Type::ref type_variable(Identifier name);
types::Type::ref type_variable(Location location);
types::Type::ref type_operator(types::Type::ref operator_,
                               types::Type::ref operand);
types::Type::ref type_operator(const types::Type::refs &xs);
types::Type::ref type_deref(types::Type::ref type);
types::Scheme::ref scheme(std::vector<std::string> vars,
                          const types::predicate_map_t &predicates,
                          types::Type::ref type);
types::TypeTuple::ref type_tuple(types::Type::refs dimensions);
types::TypeTuple::ref type_tuple(Location location,
                                 types::Type::refs dimensions);
types::Type::ref type_ptr(types::Type::ref raw);
types::Type::ref type_lambda(Identifier binding, types::Type::ref body);
types::Type::ref type_vector_type(types::Type::ref element);
types::Type::ref type_tuple_accessor(int i,
                                     int max,
                                     const std::vector<std::string> &vars);

std::string str(types::Type::refs refs);
std::string str(const types::Type::map &coll);
std::string str(const types::predicate_map_t &pm);
std::string str(const data_ctors_map_t &data_ctors_map);
std::ostream &operator<<(std::ostream &out, const types::Type::ref &type);
bool operator<(const types::Type::ref &lhs, const types::Type::ref &rhs);

types::Type::ref tuple_deref_type(Location location,
                                  types::Type::ref tuple,
                                  int index);
void unfold_binops_rassoc(std::string id,
                          types::Type::ref t,
                          types::Type::refs &unfolding);
void unfold_ops_lassoc(types::Type::ref t, types::Type::refs &unfolding);
void mutating_merge(const types::predicate_map_t::value_type &pair,
                    types::predicate_map_t &c);
void mutating_merge(const types::predicate_map_t &a, types::predicate_map_t &c);
types::predicate_map_t merge(const types::predicate_map_t &a,
                             const types::predicate_map_t &b);
types::predicate_map_t safe_merge(const types::predicate_map_t &a,
                                  const types::predicate_map_t &b);

std::ostream &join_dimensions(std::ostream &os,
                              const types::Type::refs &dimensions,
                              const types::name_index_t &name_index,
                              const types::Type::map &bindings);
std::string get_name_from_index(const types::name_index_t &name_index, int i);
bool is_valid_udt_initial_char(int ch);
