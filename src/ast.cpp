#include "ast.h"

#include "class_predicate.h"
#include "parens.h"
#include "ptr.h"
#include "zion.h"

std::ostream &operator<<(std::ostream &os, zion::ast::Program *program) {
  os << "program";
  const char *delim = "\n";
  for (auto decl : program->decls) {
    os << delim << decl;
  }
  return os << std::endl;
}

std::ostream &operator<<(std::ostream &os, zion::ast::Decl *decl) {
  os << decl->id.name << " = ";
  return decl->value->render(os, 0);
}

std::ostream &operator<<(std::ostream &os, zion::ast::Expr *expr) {
  return expr->render(os, 0);
}

namespace zion {
namespace ast {

std::string Expr::str() const {
  std::stringstream ss;
  this->render(ss, 0);
  return ss.str();
}

Location StaticPrint::get_location() const {
  return location;
}

std::ostream &StaticPrint::render(std::ostream &os,
                                  int parent_precedence) const {
  os << "(static_print ";
  expr->render(os, 0);
  return os << ")";
}

Location Var::get_location() const {
  return id.location;
}

std::ostream &Var::render(std::ostream &os, int parent_precedence) const {
  return os << C_ID << id.name << C_RESET;
}

Location As::get_location() const {
  return type->get_location();
}

std::ostream &As::render(std::ostream &os, int parent_precedence) const {
  os << "(";
  expr->render(os, 10);
  if (force_cast) {
    os << C_WARN " as! " C_RESET;
  } else {
    os << C_TYPE " as " C_RESET;
  }
  os << type->str();
  os << ")";
  return os;
}

Location Sizeof::get_location() const {
  return location;
}

std::ostream &Sizeof::render(std::ostream &os, int parent_precedence) const {
  return os << "sizeof(" << type->str() << ")";
}

Location Application::get_location() const {
  return a->get_location();
}

std::ostream &Application::render(std::ostream &os,
                                  int parent_precedence) const {
  const int precedence = 9;

  if (params.size() == 2)
    if (auto oper = dcast<const Var *>(a)) {
      if (strspn(oper->id.name.c_str(), MATHY_SYMBOLS) ==
          oper->id.name.size()) {
        os << "(";
        params[0]->render(os, precedence + 1);
        os << " ";
        oper->render(os, precedence);
        os << " ";
        params[1]->render(os, precedence + 1);
        os << ")";
        return os;
      }
    }

  Parens parens(os, parent_precedence, precedence);
  a->render(os, 10);
  os << "(";
  const char *delim = "";
  for (auto &param : params) {
    os << delim;
    param->render(os, 0);
    delim = ", ";
  }
  return os << ")";
}

Location Continue::get_location() const {
  return location;
}

std::ostream &Continue::render(std::ostream &os, int parent_precedence) const {
  return os << "(" C_CONTROL "continue!" C_RESET ")";
}

Location Break::get_location() const {
  return location;
}

std::ostream &Break::render(std::ostream &os, int parent_precedence) const {
  return os << "(" C_CONTROL "break!" C_RESET ")";
}

Location ReturnStatement::get_location() const {
  return value->get_location();
}

std::ostream &ReturnStatement::render(std::ostream &os,
                                      int parent_precedence) const {
  const int precedence = 4;
  Parens parens(os, parent_precedence, precedence);
  os << C_CONTROL "return! " C_RESET;
  value->render(os, 0);
  return os;
}

Location Match::get_location() const {
  return scrutinee->get_location();
}

std::ostream &Match::render(std::ostream &os, int parent_precedence) const {
  const int precedence = 4;
  Parens parens(os, parent_precedence, precedence);
  os << "match ";
  scrutinee->render(os, 6);
  for (auto pattern_block : pattern_blocks) {
    os << " ";
    pattern_block->render(os);
  }
  return os;
}

Location While::get_location() const {
  return condition->get_location();
}

std::ostream &While::render(std::ostream &os, int parent_precedence) const {
  const int precedence = 3;
  Parens parens(os, parent_precedence, precedence);
  os << C_CONTROL "while " C_RESET;
  condition->render(os, 6);
  os << " ";
  block->render(os, precedence);
  return os;
}

Location Block::get_location() const {
  assert(statements.size() != 0);
  return statements[0]->get_location();
}

std::ostream &Block::render(std::ostream &os, int parent_precedence) const {
  const int precedence = 0;
  Parens parens(os, parent_precedence, precedence);
  const char *delim = "";
  os << "{";
  for (auto statement : statements) {
    os << delim;
    statement->render(os, precedence);
    delim = "; ";
  }
  return os << "}";
}

Location Lambda::get_location() const {
  assert(vars.size() != 0);
  return vars[0].location;
}

std::ostream &Lambda::render(std::ostream &os, int parent_precedence) const {
  os << "(λ"
     << join_with(vars, ",", [](const Identifier &id) { return id.name; });
  os << " . ";
  body->render(os, 0);
  os << ")";
  if (return_type != nullptr) {
    os << c_good(" -> ");
    os << C_TYPE;
    return_type->emit(os, {}, 0);
    os << C_RESET;
  }
  return os;
}

Location Let::get_location() const {
  return var.location;
}

std::ostream &Let::render(std::ostream &os, int parent_precedence) const {
  const int precedence = 9;
  Parens parens(os, parent_precedence, precedence);
  os << "let " << var.name << " = ";
  value->render(os, precedence);
  os << " in ";
  body->render(os, precedence);
  return os;
}

Location Literal::get_location() const {
  return token.location;
}

std::ostream &Literal::render(std::ostream &os, int parent_precedence) const {
  return os << token.text;
}

std::ostream &Literal::render(std::ostream &os) const {
  return os << token.text;
}

Identifier Literal::instantiate_name_assignment() const {
  return Identifier{fresh(), token.location};
}

Location Tuple::get_location() const {
  return location;
}

std::ostream &Tuple::render(std::ostream &os, int parent_precedence) const {
  os << "(";
  const char *delim = "";
  for (auto dim : dims) {
    os << delim;
    dim->render(os, 0);
    delim = ", ";
  }
  if (dims.size() == 1) {
    os << ",";
  }
  return os << ")";
}

Location TupleDeref::get_location() const {
  return expr->get_location();
}

std::ostream &TupleDeref::render(std::ostream &os,
                                 int parent_precedence) const {
  const int precedence = 20;
  expr->render(os, precedence);
  return os << "[" << index << "]";
}

Location Builtin::get_location() const {
  return var->get_location();
}

std::ostream &Builtin::render(std::ostream &os, int parent_precedence) const {
  os << var->str();
  if (exprs.size() != 0) {
    os << "(";
    os << join_str(exprs, ", ");
    os << ")";
  }
  return os;
}

Location Conditional::get_location() const {
  return cond->get_location();
}

std::ostream &Conditional::render(std::ostream &os,
                                  int parent_precedence) const {
  const int precedence = 11;
  Parens parens(os, parent_precedence, precedence);
  os << "(" C_CONTROL "if " C_RESET;
  cond->render(os, precedence);
  os << C_CONTROL " then " C_RESET;
  truthy->render(os, precedence);
  os << C_CONTROL " else " C_RESET;
  falsey->render(os, precedence);
  return os << ")";
}

std::ostream &PatternBlock::render(std::ostream &os) const {
  os << "(";
  predicate->render(os);
  os << " ";
  result->render(os, 0);
  return os << ")";
}

std::string Predicate::str() const {
  std::stringstream ss;
  this->render(ss);
  return ss.str();
}

std::ostream &CtorPredicate::render(std::ostream &os) const {
  os << C_ID << ctor_name.name << C_RESET;
  if (params.size() != 0) {
    os << "(";
    const char *delim = "";
    for (auto predicate : params) {
      os << delim;
      predicate->render(os);
      delim = ", ";
    }
    os << ")";
  }
  return os;
}

Location CtorPredicate::get_location() const {
  return location;
}

Identifier CtorPredicate::instantiate_name_assignment() const {
  if (name_assignment.valid) {
    return name_assignment.t;
  } else {
    return Identifier{fresh(), location};
  }
}

std::ostream &TuplePredicate::render(std::ostream &os) const {
  os << "(";
  const char *delim = "";
  for (auto predicate : params) {
    os << delim;
    predicate->render(os);
    delim = ", ";
  }
  return os << ")";
}

Location TuplePredicate::get_location() const {
  return location;
}

Identifier TuplePredicate::instantiate_name_assignment() const {
  if (name_assignment.valid) {
    return name_assignment.t;
  } else {
    return Identifier{fresh(), location};
  }
}

std::ostream &IrrefutablePredicate::render(std::ostream &os) const {
  return os << C_ID << (name_assignment.valid ? name_assignment.t.name : "_")
            << C_RESET;
}

Location IrrefutablePredicate::get_location() const {
  return location;
}

Identifier IrrefutablePredicate::instantiate_name_assignment() const {
  if (name_assignment.valid) {
    return name_assignment.t;
  } else {
    return Identifier{fresh(), location};
  }
}

types::Ref TypeDecl::get_type() const {
  std::vector<types::Ref> types;
  assert(isupper(id.name[0]));
  types.push_back(type_id(id));
  for (auto param : params) {
    assert(islower(param.name[0]));
    types.push_back(type_variable(param));
  }
  if (types.size() >= 2) {
    return type_operator(types);
  } else {
    return types[0];
  }
}

std::string Decl::str() const {
  std::stringstream ss;
  ss << "let " << id << " = ";
  value->render(ss, 0);
  return ss.str();
}

types::SchemeRef Decl::get_early_scheme() const {
  // TODO: make this a little smarter
  return type_variable(INTERNAL_LOC())->generalize({});
}

Location Decl::get_location() const {
  return id.location;
}

TypeClass::TypeClass(Identifier id,
                     const Identifiers &type_var_ids,
                     const types::ClassPredicates &class_predicates,
                     const types::Map &overloads)
    : id(id), type_var_ids(type_var_ids), class_predicates(class_predicates),
      overloads(overloads) {
}

std::string TypeClass::str() const {
  return string_format(
      "class %s %s {\n\t%s%s\n}", id.name.c_str(),
      join_with(type_var_ids, " ", [](const Identifier &id) { return id.name; })
          .c_str(),
      class_predicates.size() != 0
          ? string_format("has %s\n\t", join(class_predicates, ", ").c_str())
                .c_str()
          : "",
      ::str(overloads).c_str());
}

Location TypeClass::get_location() const {
  return id.location;
}

std::string Instance::str() const {
  return string_format("instance %s {\n\t%s\n}", class_predicate->str().c_str(),
                       ::join_str(decls, "\n\t").c_str());
}

Location Instance::get_location() const {
  return class_predicate->get_location();
}

int next_fresh = 0;

std::string fresh() {
  return string_format("__v%d", next_fresh++);
}

} // namespace ast

ast::Expr *unit_expr(Location location) {
  return new ast::Tuple(location, {});
}

tarjan::Vertices get_free_vars(
    const ast::Expr *expr,
    const std::unordered_set<std::string> &bound_vars) {
  if (dcast<const ast::Literal *>(expr)) {
    return {};
  } else if (auto static_print = dcast<const ast::StaticPrint *>(expr)) {
    return get_free_vars(static_print->expr, bound_vars);
  } else if (auto var = dcast<const ast::Var *>(expr)) {
    if (!in(var->id.name, bound_vars)) {
      return {var->id.name};
    }
    return {};
  } else if (auto lambda = dcast<const ast::Lambda *>(expr)) {
    /* bind the params so that they do not appear as free variables from lower
     * scoping */
    auto new_bound_vars = bound_vars;
    for (auto &var : lambda->vars) {
      new_bound_vars.insert(var.name);
    }
    return get_free_vars(lambda->body, new_bound_vars);
  } else if (auto application = dcast<const ast::Application *>(expr)) {
    tarjan::Vertices free_vars = get_free_vars(application->a, bound_vars);
    for (auto &param : application->params) {
      set_merge(free_vars, get_free_vars(param, bound_vars));
    }
    return free_vars;
  } else if (auto let = dcast<const ast::Let *>(expr)) {
    tarjan::Vertices free_vars = get_free_vars(let->value, bound_vars);
    auto new_bound_vars = bound_vars;
    new_bound_vars.insert(let->var.name);
    set_merge(free_vars, get_free_vars(let->body, new_bound_vars));
    return free_vars;
  } else if (auto condition = dcast<const ast::Conditional *>(expr)) {
    tarjan::Vertices free_vars = get_free_vars(condition->cond, bound_vars);
    set_merge(free_vars, get_free_vars(condition->truthy, bound_vars));
    set_merge(free_vars, get_free_vars(condition->falsey, bound_vars));
    return free_vars;
  } else if (dcast<const ast::Break *>(expr)) {
    return {};
  } else if (dcast<const ast::Continue *>(expr)) {
    return {};
  } else if (auto while_ = dcast<const ast::While *>(expr)) {
    tarjan::Vertices free_vars = get_free_vars(while_->condition, bound_vars);
    set_merge(free_vars, get_free_vars(while_->block, bound_vars));
    return free_vars;
  } else if (auto block = dcast<const ast::Block *>(expr)) {
    tarjan::Vertices free_vars;
    for (auto statement : block->statements) {
      set_merge(free_vars, get_free_vars(statement, bound_vars));
    }
    return free_vars;
  } else if (auto return_ = dcast<const ast::ReturnStatement *>(expr)) {
    return get_free_vars(return_->value, bound_vars);
  } else if (auto tuple = dcast<const ast::Tuple *>(expr)) {
    tarjan::Vertices free_vars;
    for (auto dim : tuple->dims) {
      set_merge(free_vars, get_free_vars(dim, bound_vars));
    }
    return free_vars;
  } else if (auto tuple_deref = dcast<const ast::TupleDeref *>(expr)) {
    return get_free_vars(tuple_deref->expr, bound_vars);
  } else if (auto as = dcast<const ast::As *>(expr)) {
    return get_free_vars(as->expr, bound_vars);
  } else if (dcast<const ast::Sizeof *>(expr)) {
    return {};
  } else if (auto builtin = dcast<const ast::Builtin *>(expr)) {
    tarjan::Vertices free_vars;
    for (auto expr : builtin->exprs) {
      set_merge(free_vars, get_free_vars(expr, bound_vars));
    }
    return free_vars;
  } else if (auto match = dcast<const ast::Match *>(expr)) {
    tarjan::Vertices free_vars = get_free_vars(match->scrutinee, bound_vars);
    for (auto pattern_block : match->pattern_blocks) {
      auto new_bound_vars = bound_vars;
      pattern_block->predicate->get_bound_vars(new_bound_vars);
      set_merge(free_vars,
                get_free_vars(pattern_block->result, new_bound_vars));
    }
    return free_vars;
  } else {
    assert(false);
    return {};
  }
}

} // namespace zion
