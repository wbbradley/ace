#pragma once
#include <iostream>
#include <vector>

#include "env.h"
#include "identifier.h"
#include "infer.h"
#include "match.h"
#include "patterns.h"
#include "token.h"
#include "types.h"

struct TranslationEnv;

namespace bitter {

std::string fresh();

struct Expr {
  virtual ~Expr() throw() {
  }
  virtual Location get_location() const = 0;
  virtual std::ostream &render(std::ostream &os,
                               int parent_precedence) const = 0;
  std::string str() const;
};

struct StaticPrint : public Expr {
  StaticPrint(Location location, Expr *expr) : location(location), expr(expr) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Location location;
  Expr *const expr;
};

struct Var : public Expr {
  Var(Identifier id) : id(id) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Identifier const id;
};

struct PatternBlock {
  PatternBlock(Predicate *predicate, Expr *result)
      : predicate(predicate), result(result) {
  }
  std::ostream &render(std::ostream &os) const;

  Predicate *const predicate;
  Expr *const result;
};

using pattern_blocks_t = std::vector<PatternBlock *>;
struct Match : public Expr {
  Match(Expr *scrutinee, pattern_blocks_t pattern_blocks)
      : scrutinee(scrutinee), pattern_blocks(pattern_blocks) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Expr *const scrutinee;
  pattern_blocks_t const pattern_blocks;
};

struct Predicate {
  virtual ~Predicate() {
  }
  virtual std::ostream &render(std::ostream &os) const = 0;
  virtual match::Pattern::ref get_pattern(types::Ref type,
                                          const TranslationEnv &env) const = 0;
  virtual types::Ref tracking_infer(Env &env,
                                    Constraints &constraints) const = 0;
  virtual Location get_location() const = 0;
  virtual Identifier instantiate_name_assignment() const = 0;
  virtual void get_bound_vars(
      std::unordered_set<std::string> &bound_vars) const = 0;
  virtual Expr *translate(const DefnId &defn_id,
                          const Identifier &scrutinee_id,
                          const types::Ref &scrutinee_type,
                          bool do_checks,
                          const std::unordered_set<std::string> &bound_vars,
                          const types::TypeEnv &type_env,
                          const TranslationEnv &tenv,
                          TrackedTypes &typing,
                          NeededDefns &needed_defns,
                          bool &returns,
                          translate_continuation_t &matched,
                          translate_continuation_t &failed) const = 0;
  std::string str() const;
};

struct TuplePredicate : public Predicate {
  TuplePredicate(Location location,
                 std::vector<Predicate *> params,
                 maybe<Identifier> name_assignment)
      : location(location), params(params), name_assignment(name_assignment) {
  }
  std::ostream &render(std::ostream &os) const override;
  match::Pattern::ref get_pattern(types::Ref type,
                                  const TranslationEnv &env) const override;
  types::Ref tracking_infer(Env &env,
                            Constraints &constraints) const override;
  Identifier instantiate_name_assignment() const override;
  void get_bound_vars(
      std::unordered_set<std::string> &bound_vars) const override;
  Expr *translate(const DefnId &defn_id,
                  const Identifier &scrutinee_id,
                  const types::Ref &scrutinee_type,
                  bool do_checks,
                  const std::unordered_set<std::string> &bound_vars,
                  const types::TypeEnv &type_env,
                  const TranslationEnv &tenv,
                  TrackedTypes &typing,
                  NeededDefns &needed_defns,
                  bool &returns,
                  translate_continuation_t &matched,
                  translate_continuation_t &failed) const override;
  Location get_location() const override;

  Location const location;
  std::vector<Predicate *> const params;
  maybe<Identifier> const name_assignment;
};

struct IrrefutablePredicate : public Predicate {
  IrrefutablePredicate(Location location, maybe<Identifier> name_assignment)
      : location(location), name_assignment(name_assignment) {
  }
  std::ostream &render(std::ostream &os) const override;
  match::Pattern::ref get_pattern(types::Ref type,
                                  const TranslationEnv &env) const override;
  types::Ref tracking_infer(Env &env,
                            Constraints &constraints) const override;
  Identifier instantiate_name_assignment() const override;
  void get_bound_vars(
      std::unordered_set<std::string> &bound_vars) const override;
  Expr *translate(const DefnId &defn_id,
                  const Identifier &scrutinee_id,
                  const types::Ref &scrutinee_type,
                  bool do_checks,
                  const std::unordered_set<std::string> &bound_vars,
                  const types::TypeEnv &type_env,
                  const TranslationEnv &tenv,
                  TrackedTypes &typing,
                  NeededDefns &needed_defns,
                  bool &returns,
                  translate_continuation_t &matched,
                  translate_continuation_t &failed) const override;
  Location get_location() const override;

  Location const location;
  maybe<Identifier> const name_assignment;
};

struct CtorPredicate : public Predicate {
  CtorPredicate(Location location,
                std::vector<Predicate *> params,
                Identifier ctor_name,
                maybe<Identifier> name_assignment)
      : location(location), params(params), ctor_name(ctor_name),
        name_assignment(name_assignment) {
  }
  std::ostream &render(std::ostream &os) const override;
  match::Pattern::ref get_pattern(types::Ref type,
                                  const TranslationEnv &env) const override;
  types::Ref tracking_infer(Env &env,
                            Constraints &constraints) const override;
  Identifier instantiate_name_assignment() const override;
  void get_bound_vars(
      std::unordered_set<std::string> &bound_vars) const override;
  Expr *translate(const DefnId &defn_id,
                  const Identifier &scrutinee_id,
                  const types::Ref &scrutinee_type,
                  bool do_checks,
                  const std::unordered_set<std::string> &bound_vars,
                  const types::TypeEnv &type_env,
                  const TranslationEnv &tenv,
                  TrackedTypes &typing,
                  NeededDefns &needed_defns,
                  bool &returns,
                  translate_continuation_t &matched,
                  translate_continuation_t &failed) const override;
  Location get_location() const override;

  Location const location;
  std::vector<Predicate *> const params;
  Identifier const ctor_name;
  maybe<Identifier> const name_assignment;
};

struct Block : public Expr {
  Block(std::vector<Expr *> statements) : statements(statements) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  std::vector<Expr *> const statements;
};

struct As : public Expr {
  As(Expr *expr, types::Scheme::Ref scheme, bool force_cast)
      : expr(expr), scheme(scheme), force_cast(force_cast) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Expr *const expr;
  types::Scheme::Ref const scheme;
  bool force_cast;
};

struct Sizeof : public Expr {
  Sizeof(Location location, types::Ref type) : type(type) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Location const location;
  types::Ref const type;
};

struct Application : public Expr {
  Application(Expr *a, Expr *b) : a(a), b(b) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;
  Expr *const a;
  Expr *const b;
};

struct Lambda : public Expr {
  Lambda(Identifier var,
         types::Ref param_type,
         types::Ref return_type,
         Expr *body)
      : var(var), param_type(param_type), return_type(return_type), body(body) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Identifier const var;
  Expr *const body;
  types::Ref const param_type;
  types::Ref const return_type;
};

struct Let : public Expr {
  Let(Identifier var, Expr *value, Expr *body)
      : var(var), value(value), body(body) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Identifier const var;
  Expr *const value;
  Expr *const body;
};

struct Tuple : public Expr {
  Tuple(Location location, std::vector<Expr *> dims)
      : location(location), dims(dims) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Location const location;
  std::vector<Expr *> const dims;
};

struct TupleDeref : public Expr {
  TupleDeref(Expr *expr, int index, int max)
      : expr(expr), index(index), max(max) {
  }

  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Expr *expr;
  int index, max;
};

struct Builtin : public Expr {
  Builtin(Var *var, std::vector<Expr *> exprs) : var(var), exprs(exprs) {
  }

  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Var *var;
  std::vector<Expr *> exprs;
};

struct Literal : public Expr, public Predicate {
  Literal(Token token) : token(token) {
    // Strings are currently passed in as quoted.
    assert_implies(token.tk == tk_string, token.text[0] == '\"');
  }
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  std::ostream &render(std::ostream &os) const override;
  match::Pattern::ref get_pattern(types::Ref type,
                                  const TranslationEnv &env) const override;
  types::Ref tracking_infer(Env &env,
                            Constraints &constraints) const override;
  types::Ref non_tracking_infer() const;
  Identifier instantiate_name_assignment() const override;
  void get_bound_vars(
      std::unordered_set<std::string> &bound_vars) const override;
  Expr *translate(const DefnId &defn_id,
                  const Identifier &scrutinee_id,
                  const types::Ref &scrutinee_type,
                  bool do_checks,
                  const std::unordered_set<std::string> &bound_vars,
                  const types::TypeEnv &type_env,
                  const TranslationEnv &tenv,
                  TrackedTypes &typing,
                  NeededDefns &needed_defns,
                  bool &returns,
                  translate_continuation_t &matched,
                  translate_continuation_t &failed) const override;
  Location get_location() const override;

  Token const token;
};

struct Conditional : public Expr {
  Conditional(Expr *cond, Expr *truthy, Expr *falsey)
      : cond(cond), truthy(truthy), falsey(falsey) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Expr *const cond, *const truthy, *const falsey;
};

struct ReturnStatement : public Expr {
  ReturnStatement(Expr *value) : value(value) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Expr *const value;
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

struct While : public Expr {
  While(Expr *condition, Expr *block) : condition(condition), block(block) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Expr *const condition, *const block;
};

struct Fix : public Expr {
  Fix(Expr *f) : f(f) {
  }
  Location get_location() const override;
  std::ostream &render(std::ostream &os, int parent_precedence) const override;

  Expr *const f;
};

struct Decl {
  Decl(Identifier var, Expr *value) : var(var), value(value) {
  }
  std::string str() const;
  Location get_location() const;

  Identifier const var;
  Expr *const value;
};

struct TypeDecl {
  TypeDecl(Identifier id, const Identifiers &params) : id(id), params(params) {
  }

  Identifier const id;
  Identifiers const params;

  types::Ref get_type() const;
  int kind() const {
    return params.size() + 1;
  }
};

struct TypeClass {
  TypeClass(Identifier id,
            const Identifiers &type_var_ids,
            const types::ClassPredicates &class_predicates,
            const types::Map &overloads);

  Location get_location() const;
  std::string str() const;

  Identifier const id;
  Identifiers const type_var_ids;
  types::ClassPredicates const class_predicates;
  types::Map const overloads;
};

struct Instance {
  Instance(Identifier type_class_id,
           types::Ref type,
           const std::vector<Decl *> &decls)
      : type_class_id(type_class_id), type(type), decls(decls) {
  }
  std::string str() const;
  Location get_location() const;

  Identifier const type_class_id;
  types::Ref const type;
  std::vector<Decl *> const decls;
};

struct Module {
  Module(std::string name,
         const std::vector<Decl *> &decls,
         const std::vector<TypeDecl> &type_decls,
         const std::vector<TypeClass *> &type_classes,
         const std::vector<Instance *> &instances,
         const CtorIdMap &ctor_id_map,
         const DataCtorsMap &data_ctors_map,
         const types::TypeEnv &type_env)
      : name(name), decls(decls), type_decls(type_decls),
        type_classes(type_classes), instances(instances),
        ctor_id_map(ctor_id_map), data_ctors_map(data_ctors_map),
        type_env(type_env) {
  }

  std::string const name;
  std::vector<Decl *> const decls;
  std::vector<TypeDecl> const type_decls;
  std::vector<TypeClass *> const type_classes;
  std::vector<Instance *> const instances;
  CtorIdMap const ctor_id_map;
  DataCtorsMap const data_ctors_map;
  types::TypeEnv const type_env;
};

struct Program {
  Program(const std::vector<Decl *> &decls,
          const std::vector<TypeClass *> &type_classes,
          const std::vector<Instance *> &instances,
          Expr *expr)
      : decls(decls), type_classes(type_classes), instances(instances),
        expr(expr) {
  }

  std::vector<Decl *> const decls;
  std::vector<TypeClass *> const type_classes;
  std::vector<Instance *> const instances;
  Expr *const expr;
};
} // namespace bitter

bitter::Expr *unit_expr(Location location);
std::ostream &operator<<(std::ostream &os, bitter::Program *program);
std::ostream &operator<<(std::ostream &os, bitter::Decl *decl);
std::ostream &operator<<(std::ostream &os, bitter::Expr *expr);
