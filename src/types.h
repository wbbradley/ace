#pragma once

#include <memory>
#include <string>
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

struct type_t;

struct class_constraint_t final {
  class_constraint_t() = delete;
  class_constraint_t(std::string classname,
                     const std::vector<std::string> &tvs);
  std::string classname;
  std::vector<std::string> tvs;
  bool operator<(const class_constraint_t &rhs) const;
};

typedef std::map<std::string, int> name_index_t;
typedef std::set<class_constraint_t> class_constraints_t;
typedef std::map<std::string, std::shared_ptr<const type_t>> type_env_t;

struct signature;
struct scheme_t;

struct type_t : public std::enable_shared_from_this<type_t> {
  typedef std::shared_ptr<const type_t> ref;
  typedef std::vector<ref> refs;
  typedef std::map<std::string, ref> map;
  typedef std::pair<ref, ref> pair;

  virtual ~type_t() {
  }

  virtual std::ostream &emit(std::ostream &os,
                             const map &bindings,
                             int parent_precedence) const = 0;

  /* how many free type variables exist in this type? NB: Assumes you have
   * already bound existing bindings at the callsite prior to this check. */
  virtual int ftv_count() const = 0;

  /* NB: Also assumes you have rebound the bindings at the callsite. */
  const class_constraints_t &get_class_constraints() const;
  virtual void compute_class_constraints() const = 0;

  virtual type_t::ref eval(const type_env_t &type_env) const = 0;
  std::shared_ptr<scheme_t> generalize(
      const types::class_constraints_t &pm) const;
  std::string repr(const map &bindings) const;
  std::string repr() const {
    return this->repr({});
  }

  virtual location_t get_location() const = 0;

  std::string str() const;
  std::string str(const map &bindings) const;
  std::string get_signature() const {
    return repr();
  }

  virtual type_t::ref rebind(const map &bindings) const = 0;
  virtual type_t::ref remap_vars(
      const std::map<std::string, std::string> &map) const = 0;
  virtual type_t::ref prefix_ids(const std::set<std::string> &bindings,
                                 const std::string &pre) const = 0;
  virtual type_t::ref apply(ref type) const;

  virtual int get_precedence() const {
    return 10;
  }

protected:
  mutable bool class_constraints_valid = false;
  mutable class_constraints_t pm_;
};

struct compare_type_t {
  bool operator()(const type_t::ref &a, const type_t::ref &b) const {
    return a->repr() < b->repr();
  }
};

struct type_variable_t final : public type_t {
  type_variable_t(identifier_t id,
                  const std::set<class_constraint_t> &class_constraints);
  type_variable_t(identifier_t id);
  type_variable_t(
      location_t location /* auto-generated fresh type variables */);
  identifier_t id;
  std::set<class_constraint_t> class_constraints;

  std::ostream &emit(std::ostream &os,
                     const map &bindings,
                     int parent_precedence) const override;
  int ftv_count() const override;
  void compute_class_constraints() const override;
  type_t::ref eval(const type_env_t &type_env) const override;
  type_t::ref rebind(const map &bindings) const override;
  type_t::ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  type_t::ref prefix_ids(const std::set<std::string> &bindings,
                         const std::string &pre) const override;
  location_t get_location() const override;
};

struct type_id_t final : public type_t {
  type_id_t(identifier_t id);
  identifier_t id;

  std::ostream &emit(std::ostream &os,
                     const map &bindings,
                     int parent_precedence) const override;
  int ftv_count() const override;
  void compute_class_constraints() const override;
  type_t::ref eval(const type_env_t &type_env) const override;
  type_t::ref rebind(const map &bindings) const override;
  type_t::ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  type_t::ref prefix_ids(const std::set<std::string> &bindings,
                         const std::string &pre) const override;
  location_t get_location() const override;
};

struct type_operator_t final : public type_t {
  typedef std::shared_ptr<const type_operator_t> ref;

  type_operator_t(type_t::ref oper, type_t::ref operand);
  type_t::ref oper;
  type_t::ref operand;

  int get_precedence() const override {
    return 7;
  }

  std::ostream &emit(std::ostream &os,
                     const map &bindings,
                     int parent_precedence) const override;
  int ftv_count() const override;
  void compute_class_constraints() const override;
  type_t::ref eval(const type_env_t &type_env) const override;
  type_t::ref rebind(const map &bindings) const override;
  type_t::ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  type_t::ref prefix_ids(const std::set<std::string> &bindings,
                         const std::string &pre) const override;
  location_t get_location() const override;
};

struct type_tuple_t final : public type_t {
  typedef std::shared_ptr<const type_tuple_t> ref;

  type_tuple_t(location_t location, const type_t::refs &dimensions);

  std::ostream &emit(std::ostream &os,
                     const map &bindings,
                     int parent_precedence) const override;
  int ftv_count() const override;
  void compute_class_constraints() const override;
  type_t::ref eval(const type_env_t &type_env) const override;
  type_t::ref rebind(const map &bindings) const override;
  type_t::ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  type_t::ref prefix_ids(const std::set<std::string> &bindings,
                         const std::string &pre) const override;
  location_t get_location() const override;

  location_t location;
  type_t::refs dimensions;
};

struct type_lambda_t final : public type_t {
  type_lambda_t(identifier_t binding, type_t::ref body);
  identifier_t binding;
  type_t::ref body;

  int get_precedence() const override {
    return 6;
  }
  std::ostream &emit(std::ostream &os,
                     const map &bindings,
                     int parent_precedence) const override;
  int ftv_count() const override;
  void compute_class_constraints() const override;
  type_t::ref eval(const type_env_t &type_env) const override;
  type_t::ref rebind(const map &bindings) const override;
  type_t::ref remap_vars(
      const std::map<std::string, std::string> &map) const override;
  type_t::ref prefix_ids(const std::set<std::string> &bindings,
                         const std::string &pre) const override;
  type_t::ref apply(types::type_t::ref type) const override;
  location_t get_location() const override;
};

struct scheme_t final : public std::enable_shared_from_this<scheme_t> {
  typedef std::shared_ptr<scheme_t> ref;
  typedef std::vector<ref> refs;
  typedef std::map<std::string, ref> map;

  scheme_t(std::vector<std::string> vars,
           const class_constraints_t &class_constraints,
           types::type_t::ref type)
      : vars(vars), class_constraints(class_constraints), type(type) {
  }
  types::type_t::ref instantiate(location_t location);
  scheme_t::ref rebind(const types::type_t::map &env);
  scheme_t::ref normalize();

  /* count of the bounded type variables */
  int btvs() const;

  const class_constraints_t &get_class_constraints();
  std::string str() const;
  std::string repr() const;
  location_t get_location() const;

  std::vector<std::string> const vars;
  class_constraints_t const class_constraints;
  types::type_t::ref const type;

private:
  mutable bool class_constraints_valid = false;
  mutable class_constraints_t pm_;
};

bool is_unit(type_t::ref type);
bool is_type_id(type_t::ref type, const std::string &type_name);
type_t::refs rebind(const type_t::refs &types, const type_t::map &bindings);
type_t::ref unitize(type_t::ref type);
bool is_callable(const types::type_t::ref &type);
std::unordered_set<std::string> get_ftvs(const types::type_t::ref &type);
}; // namespace types

typedef std::map<std::string, types::type_t::map> data_ctors_map_t;
typedef std::unordered_map<const bitter::expr_t *, types::type_t::ref>
    tracked_types_t;
typedef std::unordered_map<std::string, int> ctor_id_map_t;
struct defn_ref_t {
  location_t location;
  defn_id_t from_defn_id;
};

typedef std::map<defn_id_t, std::vector<defn_ref_t>> needed_defns_t;
void insert_needed_defn(needed_defns_t &needed_defns,
                        const defn_id_t &defn_id,
                        location_t location,
                        const defn_id_t &from_defn_id);

identifier_t gensym(location_t location);

/* type data ctors */
types::type_t::ref type_bottom();
types::type_t::ref type_bool(location_t location);
types::type_t::ref type_bool(location_t location);
types::type_t::ref type_string(location_t location);
types::type_t::ref type_int(location_t location);
types::type_t::ref type_unit(location_t location);
types::type_t::ref type_null(location_t location);
types::type_t::ref type_void(location_t location);
types::type_t::ref type_map(types::type_t::ref a, types::type_t::ref b);
types::type_t::ref type_arrow(location_t location,
                              types::type_t::ref a,
                              types::type_t::ref b);
types::type_t::ref type_arrow(types::type_t::ref a, types::type_t::ref b);
types::type_t::ref type_arrows(types::type_t::refs types, int offset = 0);
types::type_t::ref type_id(identifier_t var);
types::type_t::ref type_variable(identifier_t id,
                                 const types::class_constraints_t &class_constraints);
types::type_t::ref type_variable(identifier_t name);
types::type_t::ref type_variable(location_t location);
types::type_t::ref type_operator(types::type_t::ref operator_,
                                 types::type_t::ref operand);
types::type_t::ref type_operator(const types::type_t::refs &xs);
types::type_t::ref type_deref(types::type_t::ref type);
types::scheme_t::ref scheme(std::vector<std::string> vars,
                            const types::class_constraints_t &class_constraints,
                            types::type_t::ref type);
types::type_tuple_t::ref type_tuple(types::type_t::refs dimensions);
types::type_tuple_t::ref type_tuple(location_t location,
                                    types::type_t::refs dimensions);
types::type_t::ref type_ptr(types::type_t::ref raw);
types::type_t::ref type_lambda(identifier_t binding, types::type_t::ref body);
types::type_t::ref type_vector_type(types::type_t::ref element);
types::type_t::ref type_tuple_accessor(int i,
                                       int max,
                                       const std::vector<std::string> &vars);

std::string str(types::type_t::refs refs);
std::string str(const types::type_t::map &coll);
std::string str(const types::class_constraints_t &pm);
std::string str(const data_ctors_map_t &data_ctors_map);
std::ostream &operator<<(std::ostream &out, const types::type_t::ref &type);
bool operator<(const types::type_t::ref &lhs, const types::type_t::ref &rhs);

types::type_t::ref tuple_deref_type(location_t location,
                                    types::type_t::ref tuple,
                                    int index);
void unfold_binops_rassoc(std::string id,
                          types::type_t::ref t,
                          types::type_t::refs &unfolding);
void unfold_ops_lassoc(types::type_t::ref t, types::type_t::refs &unfolding);
void mutating_merge(const types::class_constraints_t::value_type &pair,
                    types::class_constraints_t &c);
void mutating_merge(const types::class_constraints_t &a,
                    types::class_constraints_t &c);
types::class_constraints_t merge(const types::class_constraints_t &a,
                                 const types::class_constraints_t &b);
types::class_constraints_t safe_merge(const types::class_constraints_t &a,
                                      const types::class_constraints_t &b);

std::ostream &join_dimensions(std::ostream &os,
                              const types::type_t::refs &dimensions,
                              const types::name_index_t &name_index,
                              const types::type_t::map &bindings);
std::string get_name_from_index(const types::name_index_t &name_index, int i);
bool is_valid_udt_initial_char(int ch);
