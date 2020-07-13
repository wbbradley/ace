#pragma once
#include <iostream>
#include <vector>

#include "constraint.h"
#include "identifier.h"
#include "import_rules.h"
#include "infer.h"
#include "match.h"
#include "patterns.h"
#include "scheme_resolver.h"
#include "tarjan.h"
#include "token.h"
#include "types.h"

namespace zion {
namespace ast {
struct Expr;
}

ast::Expr *unit_expr(Location location);

namespace ast {

std::string fresh();

struct Expr {
  virtual ~Expr() throw() {
  }
  virtual Location get_location() const = 0;
  virtual std::ostream &render(std::ostream &os,
                               int parent_precedence) const = 0;
  std::string str() const;
};

struct Tuple : public Expr {
  Tuple(Location location, std::vector<const Expr *> dims)
      : location(location), dims(dims) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Location const location;
  std::vector<const Expr *> const dims;
};

struct StaticPrint : public Expr {
  StaticPrint(Location location, const Expr *expr)
      : location(location), expr(expr) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Location location;
  const Expr *expr;
};

struct Var : public Expr {
  Var(Identifier id) : id(id) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Identifier id;
};

struct PatternBlock {
  PatternBlock(const Predicate *predicate, const Expr *result)
      : predicate(predicate), result(result) {
  }
  std::ostream &render(std::ostream &os) const;

  const Predicate *predicate;
  const Expr *result;
};

typedef std::vector<const PatternBlock *> PatternBlocks;
struct Match : public Expr {
  Match(const Expr *scrutinee, PatternBlocks pattern_blocks)
      : scrutinee(scrutinee), pattern_blocks(pattern_blocks) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  const Expr *scrutinee;
  const PatternBlocks pattern_blocks;
};

struct Predicate {
  virtual ~Predicate() {
  }
  virtual std::ostream &render(std::ostream &os) const = 0;
  virtual match::Pattern::ref get_pattern(
      types::Ref type,
      const DataCtorsMap &data_ctors_map) const = 0;
  virtual types::Ref tracking_infer(
      const DataCtorsMap &data_ctors_map,
      const types::Ref &return_type,
      // Predicates can implicitly bind symbols, hence the following mutable
      // SchemeResolver.
      types::SchemeResolver &scheme_resolver,
      TrackedTypes &tracked_types,
      types::Constraints &constraints,
      types::ClassPredicates &instance_requirements) const = 0;
  virtual Location get_location() const = 0;
  virtual Identifier instantiate_name_assignment() const = 0;
  virtual void get_bound_vars(
      std::unordered_set<std::string> &bound_vars) const = 0;
  virtual const Expr *translate(
      const types::DefnId &defn_id,
      const Identifier &scrutinee_id,
      const types::Ref &scrutinee_type,
      bool do_checks,
      const DataCtorsMap &data_ctors_map,
      const std::unordered_set<std::string> &bound_vars,
      const TrackedTypes &tracked_types,
      const types::TypeEnv &type_env,
      TrackedTypes &typing,
      types::NeededDefns &needed_defns,
      bool &returns,
      TranslateContinuationFn &matched,
      TranslateContinuationFn &failed) const = 0;
  virtual const Predicate *rewrite(
      const RewriteImportRules &rewrite_import_rules) const = 0;
  std::string str() const;
};

struct TuplePredicate : public Predicate {
  TuplePredicate(Location location,
                 std::vector<const Predicate *> params,
                 maybe<Identifier> name_assignment)
      : location(location), params(params), name_assignment(name_assignment) {
  }
  std::ostream &render(std::ostream &os) const override;
  match::Pattern::ref get_pattern(
      types::Ref type,
      const DataCtorsMap &data_ctors_map) const override;
  types::Ref tracking_infer(
      const DataCtorsMap &data_ctors_map,
      const types::Ref &return_type,
      types::SchemeResolver &scheme_resolver,
      TrackedTypes &tracked_types,
      types::Constraints &constraints,
      types::ClassPredicates &instance_requirements) const override;
  Identifier instantiate_name_assignment() const override;
  void get_bound_vars(
      std::unordered_set<std::string> &bound_vars) const override;
  const Expr *translate(const types::DefnId &defn_id,
                        const Identifier &scrutinee_id,
                        const types::Ref &scrutinee_type,
                        bool do_checks,
                        const DataCtorsMap &data_ctors_map,
                        const std::unordered_set<std::string> &bound_vars,
                        const TrackedTypes &tracked_types,
                        const types::TypeEnv &type_env,
                        TrackedTypes &typing,
                        types::NeededDefns &needed_defns,
                        bool &returns,
                        TranslateContinuationFn &matched,
                        TranslateContinuationFn &failed) const override;
  const Predicate *rewrite(
      const RewriteImportRules &rewrite_import_rules) const override;
  Location get_location() const override;

  Location location;
  std::vector<const Predicate *> params;
  maybe<Identifier> name_assignment;
};

struct IrrefutablePredicate : public Predicate {
  IrrefutablePredicate(Location location, maybe<Identifier> name_assignment)
      : location(location), name_assignment(name_assignment) {
  }
  std::ostream &render(std::ostream &os) const override;
  match::Pattern::ref get_pattern(
      types::Ref type,
      const DataCtorsMap &data_ctors_map) const override;
  types::Ref tracking_infer(
      const DataCtorsMap &data_ctors_map,
      const types::Ref &return_type,
      types::SchemeResolver &scheme_resolver,
      TrackedTypes &tracked_types,
      types::Constraints &constraints,
      types::ClassPredicates &instance_requirements) const override;
  Identifier instantiate_name_assignment() const override;
  void get_bound_vars(
      std::unordered_set<std::string> &bound_vars) const override;
  const Expr *translate(const types::DefnId &defn_id,
                        const Identifier &scrutinee_id,
                        const types::Ref &scrutinee_type,
                        bool do_checks,
                        const DataCtorsMap &data_ctors_map,
                        const std::unordered_set<std::string> &bound_vars,
                        const TrackedTypes &tracked_types,
                        const types::TypeEnv &type_env,
                        TrackedTypes &typing,
                        types::NeededDefns &needed_defns,
                        bool &returns,
                        TranslateContinuationFn &matched,
                        TranslateContinuationFn &failed) const override;
  const Predicate *rewrite(
      const RewriteImportRules &rewrite_import_rules) const override;
  Location get_location() const override;

  Location location;
  maybe<Identifier> name_assignment;
};

struct CtorPredicate : public Predicate {
  CtorPredicate(Location location,
                std::vector<const Predicate *> params,
                Identifier ctor_name,
                maybe<Identifier> name_assignment)
      : location(location), params(params), ctor_name(ctor_name),
        name_assignment(name_assignment) {
  }
  std::ostream &render(std::ostream &os) const override;
  match::Pattern::ref get_pattern(
      types::Ref type,
      const DataCtorsMap &data_ctors_map) const override;
  types::Ref tracking_infer(
      const DataCtorsMap &data_ctors_map,
      const types::Ref &return_type,
      types::SchemeResolver &scheme_resolver,
      TrackedTypes &tracked_types,
      types::Constraints &constraints,
      types::ClassPredicates &instance_requirements) const override;
  Identifier instantiate_name_assignment() const override;
  void get_bound_vars(
      std::unordered_set<std::string> &bound_vars) const override;
  const Expr *translate(const types::DefnId &defn_id,
                        const Identifier &scrutinee_id,
                        const types::Ref &scrutinee_type,
                        bool do_checks,
                        const DataCtorsMap &data_ctors_map,
                        const std::unordered_set<std::string> &bound_vars,
                        const TrackedTypes &tracked_types,
                        const types::TypeEnv &type_env,
                        TrackedTypes &typing,
                        types::NeededDefns &needed_defns,
                        bool &returns,
                        TranslateContinuationFn &matched,
                        TranslateContinuationFn &failed) const override;
  const Predicate *rewrite(
      const RewriteImportRules &rewrite_import_rules) const override;
  Location get_location() const override;

  Location location;
  std::vector<const Predicate *> params;
  Identifier ctor_name;
  maybe<Identifier> name_assignment;
};

struct Block : public Expr {
  Block(std::vector<const Expr *> statements) : statements(statements) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  std::vector<const Expr *> const statements;
};

struct As : public Expr {
  As(const Expr *expr, types::Ref type, bool force_cast)
      : expr(expr), type(type), force_cast(force_cast) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  const Expr *expr;
  types::Ref type;
  bool force_cast;
};

struct Sizeof : public Expr {
  Sizeof(Location location, types::Ref type) : type(type) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Location location;
  types::Ref type;
};

struct Application : public Expr {
  Application(const Expr *a, const Expr *b = nullptr) : a(a), b(b ? b : unit_expr(INTERNAL_LOC())) {
  }
  Application(const Expr *a, const std::vector<const Expr *> &&tuple) : a(a) {
    if (tuple.size() == 0) {
      b = unit_expr(INTERNAL_LOC());
    } else {
      b = new Tuple(tuple[0]->get_location(), tuple);
    }
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  const Expr *a;
  const Expr *b;
};

struct Lambda : public Expr {
  Lambda(Identifier var,
         types::Ref param_type,
         types::Ref return_type,
         const Expr *body)
      : var(var), body(body), param_type(param_type),
        return_type(return_type) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Identifier var;
  const Expr *body;
  types::Ref param_type;
  types::Ref return_type;
};

struct Let : public Expr {
  Let(Identifier var, const Expr *value, const Expr *body)
      : var(var), value(value), body(body) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Identifier var;
  const Expr *value;
  const Expr *body;
};

struct TupleDeref : public Expr {
  TupleDeref(const Expr *expr, int index, int max)
      : expr(expr), index(index), max(max) {
  }

  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  const Expr *expr;
  int index, max;
};

struct FFI : public Expr {
  FFI(const Identifier id, std::vector<const Expr *> exprs)
      : id(id), exprs(exprs) {
  }

  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  const Identifier id;
  std::vector<const Expr *> exprs;
};

struct Builtin : public Expr {
  Builtin(const Var *var, std::vector<const Expr *> exprs)
      : var(var), exprs(exprs) {
  }

  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  const Var *var;
  std::vector<const Expr *> exprs;
};

struct Literal : public Expr, public Predicate {
  Literal(Token token) : token(token) {
    // Strings are currently passed in as quoted.
    assert_implies(token.tk == tk_string, token.text[0] == '\"');
  }
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  std::ostream &render(std::ostream &os) const override;
  match::Pattern::ref get_pattern(
      types::Ref type,
      const DataCtorsMap &data_ctors_map) const override;
  types::Ref tracking_infer(
      const DataCtorsMap &data_ctors_map,
      const types::Ref &return_type,
      types::SchemeResolver &scheme_resolver,
      TrackedTypes &tracked_types,
      types::Constraints &constraints,
      types::ClassPredicates &instance_requirements) const override;
  types::Ref non_tracking_infer() const;
  Identifier instantiate_name_assignment() const override;
  void get_bound_vars(
      std::unordered_set<std::string> &bound_vars) const override;
  const Expr *translate(const types::DefnId &defn_id,
                        const Identifier &scrutinee_id,
                        const types::Ref &scrutinee_type,
                        bool do_checks,
                        const DataCtorsMap &data_ctors_map,
                        const std::unordered_set<std::string> &bound_vars,
                        const TrackedTypes &tracked_types,
                        const types::TypeEnv &type_env,
                        TrackedTypes &typing,
                        types::NeededDefns &needed_defns,
                        bool &returns,
                        TranslateContinuationFn &matched,
                        TranslateContinuationFn &failed) const override;
  const Predicate *rewrite(const RewriteImportRules &) const override;
  Location get_location() const override;

  Token token;
};

struct Conditional : public Expr {
  Conditional(const Expr *cond, const Expr *truthy, const Expr *falsey)
      : cond(cond), truthy(truthy), falsey(falsey) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  const Expr *cond;
  const Expr *truthy;
  const Expr *falsey;
};

struct ReturnStatement : public Expr {
  ReturnStatement(const Expr *value) : value(value) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  const Expr *value;
};

struct Continue : public Expr {
  Continue(Location location) : location(location) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;
  Location location;
};

struct Break : public Expr {
  Break(Location location) : location(location) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;
  Location location;
};

struct Defer : public Expr {
  Defer(const Application *application) : application(application) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;
  const Application *application;
};

struct While : public Expr {
  While(const Expr *condition, const Expr *block)
      : condition(condition), block(block) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  const Expr *condition;
  const Expr *block;
};

struct Decl {
  Decl(Identifier id, const Expr *value) : id(id), value(value) {
    assert(id.name.find("0x7") == std::string::npos);
  }
  std::string str() const;
  Location get_location() const;
  types::SchemeRef get_early_scheme() const;

  Identifier id;
  const Expr *const value;
};

struct TypeDecl {
  TypeDecl(Identifier id, const Identifiers &params) : id(id), params(params) {
  }

  Identifier id;
  Identifiers params;

  types::Ref get_type() const;
  int kind() const {
    return params.size() + 1;
  }
};

struct TypeClass {
  TypeClass(Identifier id,
            const Identifiers &type_var_ids,
            const types::ClassPredicates &class_predicates,
            const types::Map &overloads,
            const std::vector<const Decl *> &default_decls);

  Location get_location() const;
  std::string str() const;

  Identifier id;
  Identifiers type_var_ids;
  types::ClassPredicates class_predicates;
  types::Map overloads;
  std::vector<const Decl *> default_decls;
};

struct Instance {
  Instance(const types::ClassPredicateRef &class_predicate,
           const std::vector<const Decl *> &decls)
      : class_predicate(class_predicate), decls(decls) {
  }
  std::string str() const;
  Location get_location() const;

  types::ClassPredicateRef const class_predicate;
  std::vector<const Decl *> const decls;
};

struct Module {
  Module(std::string name,
         const std::vector<Identifier> &imports,
         const std::vector<const Decl *> &decls,
         const std::vector<const TypeDecl *> &type_decls,
         const std::vector<const TypeClass *> &type_classes,
         const std::vector<const Instance *> &instances,
         const ParsedCtorIdMap &ctor_id_map,
         const ParsedDataCtorsMap &data_ctors_map,
         const types::TypeEnv &type_env)
      : name(name), imports(imports), decls(decls), type_decls(type_decls),
        type_classes(type_classes), instances(instances),
        ctor_id_map(ctor_id_map), data_ctors_map(data_ctors_map),
        type_env(type_env) {
  }

  std::string const name;
  std::vector<Identifier> imports;
  std::vector<const Decl *> decls;
  std::vector<const TypeDecl *> type_decls;
  std::vector<const TypeClass *> type_classes;
  std::vector<const Instance *> instances;
  ParsedCtorIdMap ctor_id_map;
  ParsedDataCtorsMap data_ctors_map;
  types::TypeEnv const type_env;
};

struct Program {
  Program(const std::vector<const Decl *> &decls,
          const std::vector<const TypeClass *> &type_classes,
          const std::vector<const Instance *> &instances,
          const Expr *expr)
      : decls(decls), type_classes(type_classes), instances(instances),
        expr(expr) {
  }

  std::vector<const Decl *> decls;
  std::vector<const TypeClass *> type_classes;
  std::vector<const Instance *> instances;
  const Expr *const expr;
};
} // namespace ast

tarjan::Vertices get_free_vars(
    const ast::Expr *expr,
    const std::unordered_set<std::string> &bound_vars);
} // namespace zion

std::ostream &operator<<(std::ostream &os, zion::ast::Program *program);
std::ostream &operator<<(std::ostream &os, zion::ast::Decl *decl);
std::ostream &operator<<(std::ostream &os, zion::ast::Expr *expr);
