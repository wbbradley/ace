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

namespace types {

struct Type;
struct Scheme;
struct ClassPredicate;

typedef std::set<std::string> Ftvs;
typedef std::map<std::string, int> NameIndex;
typedef std::unordered_set<std::shared_ptr<const ClassPredicate>>
    ClassPredicates;
typedef std::map<std::string, std::shared_ptr<const Type>> TypeEnv;
typedef std::shared_ptr<const Type> Ref;
typedef std::vector<Ref> Refs;
typedef std::map<std::string, Ref> Map;
typedef std::pair<Ref, Ref> Pair;

bool is_unit(TypeRef type);
bool is_type_id(TypeRef type, const std::string &type_name);
TypeRefs rebind(const TypeRefs &types, const Map &bindings);
TypeRef unitize(TypeRef type);
bool is_callable(const types::TypeRef &type);
std::unordered_set<std::string> get_ftvs(const types::TypeRef &type);

}; // namespace types

typedef std::map<std::string, types::Map> DataCtorsMap;
typedef std::unordered_map<const bitter::Expr *, types::TypeRef> TrackedTypes;
typedef std::unordered_map<std::string, int> CtorIdMap;

struct DefnRef {
  Location const location;
  DefnId const from_defn_id;
};

typedef std::map<DefnId, std::vector<DefnRef>> NeededDefns;
void insert_needed_defn(NeededDefns &needed_defns,
                        const DefnId &defn_id,
                        Location location,
                        const DefnId &from_defn_id);

Identifier gensym(Location location);

/* type data ctors */
types::TypeRef type_bottom();
types::TypeRef type_bool(Location location);
types::TypeRef type_bool(Location location);
types::TypeRef type_string(Location location);
types::TypeRef type_int(Location location);
types::TypeRef type_unit(Location location);
types::TypeRef type_null(Location location);
types::TypeRef type_void(Location location);
types::TypeRef type_map(types::TypeRef a, types::TypeRef b);
types::TypeRef type_arrow(Location location,
                          types::TypeRef a,
                          types::TypeRef b);
types::TypeRef type_arrow(types::TypeRef a, types::TypeRef b);
types::TypeRef type_arrows(types::TypeRefs types, int offset = 0);
types::TypeRef type_id(Identifier var);
types::TypeRef type_variable(Identifier id,
                             const std::set<std::string> &predicates);
types::TypeRef type_variable(Identifier name);
types::TypeRef type_variable(Location location);
types::TypeRef type_operator(types::TypeRef operator_, types::TypeRef operand);
types::TypeRef type_operator(const types::TypeRefs &xs);
types::TypeRef type_deref(types::TypeRef type);
types::Scheme::Ref scheme(std::vector<std::string> vars,
                          const types::ClassPredicates &predicates,
                          types::TypeRef type);
types::TypeTuple::Ref type_tuple(types::TypeRefs dimensions);
types::TypeTuple::Ref type_tuple(Location location, types::TypeRefs dimensions);
types::TypeRef type_ptr(types::TypeRef raw);
types::TypeRef type_lambda(Identifier binding, types::TypeRef body);
types::TypeRef type_vector_type(types::TypeRef element);
types::TypeRef type_tuple_accessor(int i,
                                   int max,
                                   const std::vector<std::string> &vars);

std::string str(types::TypeRefs Refs);
std::string str(const types::Map &coll);
std::string str(const types::ClassPredicates &pm);
std::string str(const DataCtorsMap &data_ctors_map);
std::ostream &operator<<(std::ostream &out, const types::TypeRef &type);
bool operator<(const types::TypeRef &lhs, const types::TypeRef &rhs);

types::TypeRef tuple_deref_type(Location location,
                                types::TypeRef tuple,
                                int index);
void unfold_binops_rassoc(std::string id,
                          types::TypeRef t,
                          types::TypeRefs &unfolding);
void unfold_ops_lassoc(types::TypeRef t, types::TypeRefs &unfolding);
void mutating_merge(const types::ClassPredicates::value_type &pair,
                    types::ClassPredicates &c);
void mutating_merge(const types::ClassPredicates &a, types::ClassPredicates &c);
types::ClassPredicates merge(const types::ClassPredicates &a,
                             const types::ClassPredicates &b);

std::ostream &join_dimensions(std::ostream &os,
                              const types::TypeRefs &dimensions,
                              const types::NameIndex &name_index,
                              const types::Map &bindings);
std::string get_name_from_index(const types::NameIndex &name_index, int i);
bool is_valid_udt_initial_char(int ch);
