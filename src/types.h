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

typedef std::map<std::string, int> NameIndex;
typedef std::unordered_set<ClassPredicate::Ref> ClassPredicates;
typedef std::map<std::string, std::shared_ptr<const Type>> type_env_t;

struct signature;
struct Scheme;

struct Type : public std::enable_shared_from_this<Type> {
  typedef std::shared_ptr<const Type> Ref;
  typedef std::vector<Ref> refs;
  typedef std::map<std::string, Ref> map;
  typedef std::pair<Ref, Ref> pair;

  virtual ~Type() {
  }

  virtual std::ostream &emit(std::ostream &os,
                             const map &bindings,
                             int parent_precedence) const = 0;

  /* how many free type variables exist in this type? NB: Assumes you have
   * already bound existing bindings at the callsite prior to this check. */
  virtual int ftv_count() const = 0;

  /* NB: Also assumes you have rebound the bindings at the callsite. */
  const std::unordered_set<std::string> &get_ftvs() const;
  virtual void compute_ftvs() const = 0;

  virtual Type::Ref eval(const type_env_t &type_env) const = 0;
  std::shared_ptr<Scheme> generalize(const types::ClassPredicates &pm) const;
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

  virtual Type::Ref rebind(const map &bindings) const = 0;
  virtual Type::Ref remap_vars(
      const std::map<std::string, std::string> &map) const = 0;
  virtual Type::Ref prefix_ids(const std::set<std::string> &bindings,
                               const std::string &pre) const = 0;
  virtual Type::Ref apply(Ref type) const;

  virtual int get_precedence() const {
    return 10;
  }

protected:
  mutable bool ftvs_valid_ = false;
  mutable std::unordered_set<std::string> ftvs_;
};

struct CompareType {
  bool operator()(const Type::Ref &a, const Type::Ref &b) const {
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
  Type::Ref eval(const type_env_t &type_env) const override;
  Type::Ref rebind(const map &bindings) const override;
  Type::Ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  Type::Ref prefix_ids(const std::set<std::string> &bindings,
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
  Type::Ref eval(const type_env_t &type_env) const override;
  Type::Ref rebind(const map &bindings) const override;
  Type::Ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  Type::Ref prefix_ids(const std::set<std::string> &bindings,
                       const std::string &pre) const override;
  Location get_location() const override;
};

struct TypeOperator final : public Type {
  typedef std::shared_ptr<const TypeOperator> Ref;

  TypeOperator(Type::Ref oper, Type::Ref operand);
  Type::Ref oper;
  Type::Ref operand;

  int get_precedence() const override {
    return 7;
  }

  std::ostream &emit(std::ostream &os,
                     const map &bindings,
                     int parent_precedence) const override;
  int ftv_count() const override;
  void compute_predicate_map() const override;
  Type::Ref eval(const type_env_t &type_env) const override;
  Type::Ref rebind(const map &bindings) const override;
  Type::Ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  Type::Ref prefix_ids(const std::set<std::string> &bindings,
                       const std::string &pre) const override;
  Location get_location() const override;
};

struct TypeTuple final : public Type {
  typedef std::shared_ptr<const TypeTuple> Ref;

  TypeTuple(Location location, const Type::refs &dimensions);

  std::ostream &emit(std::ostream &os,
                     const map &bindings,
                     int parent_precedence) const override;
  int ftv_count() const override;
  void compute_predicate_map() const override;
  Type::Ref eval(const type_env_t &type_env) const override;
  Type::Ref rebind(const map &bindings) const override;
  Type::Ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  Type::Ref prefix_ids(const std::set<std::string> &bindings,
                       const std::string &pre) const override;
  Location get_location() const override;

  Location location;
  Type::refs dimensions;
};

struct TypeLambda final : public Type {
  TypeLambda(Identifier binding, Type::Ref body);
  Identifier binding;
  Type::Ref body;

  int get_precedence() const override {
    return 6;
  }
  std::ostream &emit(std::ostream &os,
                     const map &bindings,
                     int parent_precedence) const override;
  int ftv_count() const override;
  void compute_predicate_map() const override;
  Type::Ref eval(const type_env_t &type_env) const override;
  Type::Ref rebind(const map &bindings) const override;
  Type::Ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  Type::Ref prefix_ids(const std::set<std::string> &bindings,
                       const std::string &pre) const override;
  Type::Ref apply(types::Type::Ref type) const override;
  Location get_location() const override;
};

struct Scheme final : public std::enable_shared_from_this<Scheme> {
  typedef std::shared_ptr<Scheme> Ref;
  typedef std::vector<Ref> refs;
  typedef std::map<std::string, Ref> map;

  Scheme(std::vector<std::string> vars,
         const ClassPredicates &predicates,
         types::Type::Ref type)
      : vars(vars), predicates(predicates), type(type) {
  }
  types::Type::Ref instantiate(Location location);
  Scheme::Ref rebind(const types::Type::map &env);
  Scheme::Ref normalize();

  /* count of the bounded type variables */
  int btvs() const;

  const ClassPredicates &get_predicate_map();
  std::string str() const;
  std::string repr() const;
  Location get_location() const;

  std::vector<std::string> const vars;
  ClassPredicates const predicates;
  types::Type::Ref const type;

private:
  mutable bool ftvs_valid_ = false;
  mutable ClassPredicates pm_;
};

bool is_unit(Type::Ref type);
bool is_type_id(Type::Ref type, const std::string &type_name);
Type::refs rebind(const Type::refs &types, const Type::map &bindings);
Type::Ref unitize(Type::Ref type);
bool is_callable(const types::Type::Ref &type);
std::unordered_set<std::string> get_ftvs(const types::Type::Ref &type);
}; // namespace types

typedef std::map<std::string, types::Type::map> data_ctors_map_t;
typedef std::unordered_map<const bitter::Expr *, types::Type::Ref>
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
types::Type::Ref type_bottom();
types::Type::Ref type_bool(Location location);
types::Type::Ref type_bool(Location location);
types::Type::Ref type_string(Location location);
types::Type::Ref type_int(Location location);
types::Type::Ref type_unit(Location location);
types::Type::Ref type_null(Location location);
types::Type::Ref type_void(Location location);
types::Type::Ref type_map(types::Type::Ref a, types::Type::Ref b);
types::Type::Ref type_arrow(Location location,
                            types::Type::Ref a,
                            types::Type::Ref b);
types::Type::Ref type_arrow(types::Type::Ref a, types::Type::Ref b);
types::Type::Ref type_arrows(types::Type::refs types, int offset = 0);
types::Type::Ref type_id(Identifier var);
types::Type::Ref type_variable(Identifier id,
                               const std::set<std::string> &predicates);
types::Type::Ref type_variable(Identifier name);
types::Type::Ref type_variable(Location location);
types::Type::Ref type_operator(types::Type::Ref operator_,
                               types::Type::Ref operand);
types::Type::Ref type_operator(const types::Type::refs &xs);
types::Type::Ref type_deref(types::Type::Ref type);
types::Scheme::Ref scheme(std::vector<std::string> vars,
                          const types::ClassPredicates &predicates,
                          types::Type::Ref type);
types::TypeTuple::Ref type_tuple(types::Type::refs dimensions);
types::TypeTuple::Ref type_tuple(Location location,
                                 types::Type::refs dimensions);
types::Type::Ref type_ptr(types::Type::Ref raw);
types::Type::Ref type_lambda(Identifier binding, types::Type::Ref body);
types::Type::Ref type_vector_type(types::Type::Ref element);
types::Type::Ref type_tuple_accessor(int i,
                                     int max,
                                     const std::vector<std::string> &vars);

std::string str(types::Type::refs refs);
std::string str(const types::Type::map &coll);
std::string str(const types::ClassPredicates &pm);
std::string str(const data_ctors_map_t &data_ctors_map);
std::ostream &operator<<(std::ostream &out, const types::Type::Ref &type);
bool operator<(const types::Type::Ref &lhs, const types::Type::Ref &rhs);

types::Type::Ref tuple_deref_type(Location location,
                                  types::Type::Ref tuple,
                                  int index);
void unfold_binops_rassoc(std::string id,
                          types::Type::Ref t,
                          types::Type::refs &unfolding);
void unfold_ops_lassoc(types::Type::Ref t, types::Type::refs &unfolding);
void mutating_merge(const types::ClassPredicates::value_type &pair,
                    types::ClassPredicates &c);
void mutating_merge(const types::ClassPredicates &a, types::ClassPredicates &c);
types::ClassPredicates merge(const types::ClassPredicates &a,
                             const types::ClassPredicates &b);
types::ClassPredicates safe_merge(const types::ClassPredicates &a,
                                  const types::ClassPredicates &b);

std::ostream &join_dimensions(std::ostream &os,
                              const types::Type::refs &dimensions,
                              const types::NameIndex &name_index,
                              const types::Type::map &bindings);
std::string get_name_from_index(const types::NameIndex &name_index, int i);
bool is_valid_udt_initial_char(int ch);
