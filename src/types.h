#pragma once

#include <memory>
#include <unordered_set>

#include "ast_decls.h"
#include "defn_id.h"
#include "identifier.h"
#include "utils.h"
#include "zion.h"

namespace types {

struct Type;
struct Scheme;
struct ClassPredicate;

typedef std::set<std::string> Ftvs;
typedef std::map<std::string, int> NameIndex;
typedef std::shared_ptr<const ClassPredicate> ClassPredicateRef;
typedef std::unordered_set<ClassPredicateRef> ClassPredicates;
typedef std::map<std::string, std::shared_ptr<const Type>> TypeEnv;
typedef std::shared_ptr<const Type> Ref;
typedef std::vector<Ref> Refs;
typedef std::map<std::string, Ref> Map;
typedef std::pair<Ref, Ref> Pair;

struct Type : public std::enable_shared_from_this<Type> {
  virtual ~Type() {
  }

  virtual std::ostream &emit(std::ostream &os,
                             const Map &bindings,
                             int parent_precedence) const = 0;

  int ftv_count() const;
  const Ftvs &get_ftvs() const;

  virtual void compute_ftvs() const = 0;

  virtual Ref eval(const TypeEnv &type_env) const = 0;
  std::shared_ptr<Scheme> generalize(const types::ClassPredicates &pm) const;
  std::string repr(const Map &bindings) const;
  std::string repr() const {
    return this->repr({});
  }

  virtual Location get_location() const = 0;

  std::string str() const;
  std::string str(const Map &bindings) const;
  std::string get_signature() const {
    return repr();
  }

  virtual Ref rebind(const Map &bindings) const = 0;
  virtual Ref remap_vars(
      const std::map<std::string, std::string> &map) const = 0;
  virtual Ref prefix_ids(const std::set<std::string> &bindings,
                         const std::string &pre) const = 0;
  virtual Ref apply(Ref type) const;

  virtual int get_precedence() const {
    return 10;
  }

private:
  mutable bool ftvs_valid_ = false;

protected:
  mutable Ftvs ftvs_;
};

struct CompareType {
  bool operator()(const Ref &a, const Ref &b) const {
    return a->repr() < b->repr();
  }
};

struct TypeVariable final : public Type {
  TypeVariable(Identifier id);
  TypeVariable(Location location /* auto-generated fresh type variables */);

  Identifier id;

  std::ostream &emit(std::ostream &os,
                     const Map &bindings,
                     int parent_precedence) const override;
  void compute_ftvs() const override;
  Ref eval(const TypeEnv &type_env) const override;
  Ref rebind(const Map &bindings) const override;
  Ref remap_vars(const std::map<std::string, std::string> &map) const override;
  Ref prefix_ids(const std::set<std::string> &bindings,
                 const std::string &pre) const override;
  Location get_location() const override;
};

struct TypeId final : public Type {
  TypeId(Identifier id);
  Identifier id;

  std::ostream &emit(std::ostream &os,
                     const Map &bindings,
                     int parent_precedence) const override;
  void compute_ftvs() const override;
  Ref eval(const TypeEnv &type_env) const override;
  Ref rebind(const Map &bindings) const override;
  Ref remap_vars(const std::map<std::string, std::string> &map) const override;
  Ref prefix_ids(const std::set<std::string> &bindings,
                 const std::string &pre) const override;
  Location get_location() const override;
};

struct TypeOperator final : public Type {
  typedef std::shared_ptr<const TypeOperator> Ref;

  TypeOperator(Ref oper, Ref operand);
  Ref oper;
  Ref operand;

  int get_precedence() const override {
    return 7;
  }

  std::ostream &emit(std::ostream &os,
                     const Map &bindings,
                     int parent_precedence) const override;
  void compute_ftvs() const override;
  types::Ref eval(const TypeEnv &type_env) const override;
  types::Ref rebind(const Map &bindings) const override;
  types::Ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  types::Ref prefix_ids(const std::set<std::string> &bindings,
                        const std::string &pre) const override;
  Location get_location() const override;
};

struct TypeTuple final : public Type {
  typedef std::shared_ptr<const TypeTuple> Ref;

  TypeTuple(Location location, const Refs &dimensions);

  std::ostream &emit(std::ostream &os,
                     const Map &bindings,
                     int parent_precedence) const override;
  void compute_ftvs() const override;
  types::Ref eval(const TypeEnv &type_env) const override;
  types::Ref rebind(const Map &bindings) const override;
  types::Ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  types::Ref prefix_ids(const std::set<std::string> &bindings,
                        const std::string &pre) const override;
  Location get_location() const override;

  Location location;
  Refs dimensions;
};

struct TypeLambda final : public Type {
  TypeLambda(Identifier binding, Ref body);
  Identifier binding;
  Ref body;

  int get_precedence() const override {
    return 6;
  }
  std::ostream &emit(std::ostream &os,
                     const Map &bindings,
                     int parent_precedence) const override;
  void compute_ftvs() const override;
  Ref eval(const TypeEnv &type_env) const override;
  Ref rebind(const Map &bindings) const override;
  Ref remap_vars(const std::map<std::string, std::string> &map) const override;
  Ref prefix_ids(const std::set<std::string> &bindings,
                 const std::string &pre) const override;
  Ref apply(types::Ref type) const override;
  Location get_location() const override;
};

struct Scheme final : public std::enable_shared_from_this<Scheme> {
  typedef std::shared_ptr<Scheme> Ref;
  typedef std::vector<Ref> Refs;
  typedef std::map<std::string, Ref> Map;

  Scheme(std::vector<std::string> vars,
         const ClassPredicates &predicates,
         types::Ref type)
      : vars(vars), predicates(predicates), type(type) {
  }
  types::Ref instantiate(Location location);
  Scheme::Ref rebind(const types::Map &env);
  Scheme::Ref normalize();

  /* count of the bounded type variables */
  int btvs() const;

  const ClassPredicates &get_class_predicates();
  std::string str() const;
  std::string repr() const;
  Location get_location() const;

  std::vector<std::string> const vars;
  ClassPredicates const predicates;
  types::Ref const type;

private:
  mutable bool ftvs_valid_ = false;
  mutable ClassPredicates pm_;
};

bool is_unit(Ref type);
bool is_type_id(Ref type, const std::string &type_name);
Refs rebind(const Refs &types, const Map &bindings);
Ref unitize(Ref type);
bool is_callable(const types::Ref &type);
std::unordered_set<std::string> get_ftvs(const types::Ref &type);
}; // namespace types

typedef std::map<std::string, types::Map> DataCtorsMap;
typedef std::unordered_map<const bitter::Expr *, types::Ref> TrackedTypes;
typedef std::unordered_map<std::string, int> CtorIdMap;
struct DefnRef {
  Location location;
  DefnId from_defn_id;
};

typedef std::map<DefnId, std::vector<DefnRef>> NeededDefns;
void insert_needed_defn(NeededDefns &needed_defns,
                        const DefnId &defn_id,
                        Location location,
                        const DefnId &from_defn_id);

Identifier gensym(Location location);

/* type data ctors */
types::Ref type_bottom();
types::Ref type_bool(Location location);
types::Ref type_bool(Location location);
types::Ref type_string(Location location);
types::Ref type_int(Location location);
types::Ref type_unit(Location location);
types::Ref type_null(Location location);
types::Ref type_void(Location location);
types::Ref type_map(types::Ref a, types::Ref b);
types::Ref type_arrow(Location location, types::Ref a, types::Ref b);
types::Ref type_arrow(types::Ref a, types::Ref b);
types::Ref type_arrows(types::Refs types, int offset = 0);
types::Ref type_id(Identifier var);
types::Ref type_variable(Identifier id,
                         const std::set<std::string> &predicates);
types::Ref type_variable(Identifier name);
types::Ref type_variable(Location location);
types::Ref type_operator(types::Ref operator_, types::Ref operand);
types::Ref type_operator(const types::Refs &xs);
types::Ref type_deref(types::Ref type);
types::Scheme::Ref scheme(std::vector<std::string> vars,
                          const types::ClassPredicates &predicates,
                          types::Ref type);
types::TypeTuple::Ref type_tuple(types::Refs dimensions);
types::TypeTuple::Ref type_tuple(Location location, types::Refs dimensions);
types::Ref type_ptr(types::Ref raw);
types::Ref type_lambda(Identifier binding, types::Ref body);
types::Ref type_vector_type(types::Ref element);
types::Ref type_tuple_accessor(int i,
                               int max,
                               const std::vector<std::string> &vars);

std::string str(types::Refs Refs);
std::string str(const types::Map &coll);
std::string str(const types::ClassPredicates &pm);
std::string str(const DataCtorsMap &data_ctors_map);
std::ostream &operator<<(std::ostream &out, const types::Ref &type);
bool operator<(const types::Ref &lhs, const types::Ref &rhs);

types::Ref tuple_deref_type(Location location, types::Ref tuple, int index);
void unfold_binops_rassoc(std::string id, types::Ref t, types::Refs &unfolding);
void unfold_ops_lassoc(types::Ref t, types::Refs &unfolding);
void mutating_merge(const types::ClassPredicates::value_type &pair,
                    types::ClassPredicates &c);
void mutating_merge(const types::ClassPredicates &a, types::ClassPredicates &c);
types::ClassPredicates merge(const types::ClassPredicates &a,
                             const types::ClassPredicates &b);
types::ClassPredicates safe_merge(const types::ClassPredicates &a,
                                  const types::ClassPredicates &b);

std::ostream &join_dimensions(std::ostream &os,
                              const types::Refs &dimensions,
                              const types::NameIndex &name_index,
                              const types::Map &bindings);
std::string get_name_from_index(const types::NameIndex &name_index, int i);
bool is_valid_udt_initial_char(int ch);
