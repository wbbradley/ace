#include "prefix.h"

#include "class_predicate.h"
#include "ptr.h"
#include "tld.h"

namespace zion {

using namespace ast;

std::string prefix(const std::set<std::string> &bindings,
                   std::string pre,
                   std::string name) {
  auto names = tld::split_fqn(name);
  if (names.size() > 1) {
    return name;
  }

  if (in(name, bindings)) {
    return tld::mktld(pre, name);
  } else {
    return name;
  }
}

Identifier prefix(const std::set<std::string> &bindings,
                  std::string pre,
                  const Identifier &id) {
  return Identifier{prefix(bindings, pre, id.name), id.location};
}

Token prefix(const std::set<std::string> &bindings,
             std::string pre,
             Token name) {
  assert(name.tk == tk_identifier);
  return Token{name.location, tk_identifier, prefix(bindings, pre, name.text)};
}

Expr *prefix(const std::set<std::string> &bindings,
             std::string pre,
             Expr *value);

const Predicate *prefix(const std::set<std::string> &bindings,
                        std::string pre,
                        const Predicate *predicate,
                        std::set<std::string> &new_symbols) {
  if (auto p = dcast<const TuplePredicate *>(predicate)) {
    if (p->name_assignment.valid) {
      new_symbols.insert(p->name_assignment.t.name);
    }
    std::vector<const Predicate *> new_params;
    for (auto param : p->params) {
      new_params.push_back(prefix(bindings, pre, param, new_symbols));
    }
    return new TuplePredicate(p->location, new_params, p->name_assignment);
  } else if (auto p = dcast<const IrrefutablePredicate *>(predicate)) {
    if (p->name_assignment.valid) {
      new_symbols.insert(p->name_assignment.t.name);
    }
    return predicate;
  } else if (auto p = dcast<const CtorPredicate *>(predicate)) {
    if (p->name_assignment.valid) {
      new_symbols.insert(p->name_assignment.t.name);
    }
    std::vector<const Predicate *> new_params;
    for (auto param : p->params) {
      new_params.push_back(prefix(bindings, pre, param, new_symbols));
    }
    return new CtorPredicate(p->location, new_params,
                             prefix(bindings, pre, p->ctor_name),
                             p->name_assignment);
  } else if (auto p = dcast<const Literal *>(predicate)) {
    return p;
  } else {
    assert(false);
    return nullptr;
  }
}

const PatternBlock *prefix(const std::set<std::string> &bindings,
                           std::string pre,
                           const PatternBlock *pattern_block) {
  std::set<std::string> new_symbols;
  const Predicate *new_predicate = prefix(
      bindings, pre, pattern_block->predicate, new_symbols);

  return new PatternBlock(new_predicate, prefix(set_diff(bindings, new_symbols),
                                                pre, pattern_block->result));
}

const Decl *prefix(const std::set<std::string> &bindings,
                   std::string pre,
                   const Decl *value) {
  return new Decl(prefix(bindings, pre, value->id),
                  prefix(bindings, pre, value->value));
}

const TypeDecl *prefix(const std::set<std::string> &bindings,
                       std::string pre,
                       const TypeDecl *type_decl) {
  return new TypeDecl{prefix(bindings, pre, type_decl->id), type_decl->params};
}

std::set<std::string> only_type_names(const std::set<std::string> &bindings) {
  std::set<std::string> only_type_names;
  for (auto binding : bindings) {
    if (tld::is_tld_type(binding)) {
      only_type_names.insert(binding);
    }
  }
  return only_type_names;
}

const TypeClass *prefix(const std::set<std::string> &bindings,
                        std::string pre,
                        const TypeClass *type_class) {
  return new TypeClass(
      prefix(bindings, pre, type_class->id), type_class->type_var_ids,
      prefix(bindings, pre, type_class->class_predicates),
      prefix(bindings, pre, type_class->overloads, true /*include_keys*/),
      prefix(bindings, pre, type_class->default_decls));
}

types::ClassPredicateRef prefix(
    const std::set<std::string> &bindings,
    std::string pre,
    const types::ClassPredicateRef &class_predicate) {
  return std::make_shared<types::ClassPredicate>(
      prefix(bindings, pre, class_predicate->classname),
      prefix(bindings, pre, class_predicate->params));
}

types::ClassPredicates prefix(const std::set<std::string> &bindings,
                              std::string pre,
                              const types::ClassPredicates &class_predicates) {
  types::ClassPredicates new_cps;
  for (auto &cp : class_predicates) {
    new_cps.insert(prefix(bindings, pre, cp));
  }
  return new_cps;
}

const Instance *prefix(const std::set<std::string> &bindings,
                       std::string pre,
                       const Instance *instance) {
  return new Instance(prefix(bindings, pre, instance->class_predicate),
                      prefix(bindings, pre, instance->decls));
}

types::Ref prefix(const std::set<std::string> &bindings,
                  std::string pre,
                  types::Ref type) {
  if (type == nullptr) {
    return nullptr;
  }

  std::set<std::string> uppercase_bindings = only_type_names(bindings);
  return type->prefix_ids(uppercase_bindings, pre);
}

std::set<std::string> without(const std::set<std::string> &s,
                              const Identifiers &vars) {
  std::set<std::string> c = s;
  for (auto &var : vars) {
    c.erase(var.name);
  }
  return c;
}

const Application *prefix_application(const std::set<std::string> &bindings,
                                      std::string pre,
                                      const Application *application) {
  return new Application(prefix(bindings, pre, application->a),
                         prefix(bindings, pre, application->params));
}

const Expr *prefix(const std::set<std::string> &bindings,
                   std::string pre,
                   const Expr *value) {
  if (auto static_print = dcast<const StaticPrint *>(value)) {
    return new StaticPrint(static_print->location,
                           prefix(bindings, pre, static_print->expr));
  } else if (auto var = dcast<const Var *>(value)) {
    return new Var(prefix(bindings, pre, var->id));
  } else if (auto match = dcast<const Match *>(value)) {
    return new Match(prefix(bindings, pre, match->scrutinee),
                     prefix(bindings, pre, match->pattern_blocks),
                     match->disable_coverage_check);
  } else if (auto block = dcast<const Block *>(value)) {
    return new Block(prefix(bindings, pre, block->statements));
  } else if (auto as = dcast<const As *>(value)) {
    return new As(prefix(bindings, pre, as->expr),
                  prefix(bindings, pre, as->type), as->force_cast);
  } else if (auto application = dcast<const Application *>(value)) {
    return prefix_application(bindings, pre, application);
  } else if (auto lambda = dcast<const Lambda *>(value)) {
    return new Lambda(
        lambda->vars, prefix(bindings, pre, lambda->param_types),
        prefix(bindings, pre, lambda->return_type),
        prefix(without(bindings, lambda->vars), pre, lambda->body));
  } else if (auto let = dcast<const Let *>(value)) {
    return new Let(let->var,
                   prefix(::without(bindings, let->var.name), pre, let->value),
                   prefix(::without(bindings, let->var.name), pre, let->body));
  } else if (auto conditional = dcast<const Conditional *>(value)) {
    return new Conditional(prefix(bindings, pre, conditional->cond),
                           prefix(bindings, pre, conditional->truthy),
                           prefix(bindings, pre, conditional->falsey));
  } else if (auto ret = dcast<const ReturnStatement *>(value)) {
    return new ReturnStatement(prefix(bindings, pre, ret->value));
  } else if (auto while_ = dcast<const While *>(value)) {
    return new While(prefix(bindings, pre, while_->condition),
                     prefix(bindings, pre, while_->block));
  } else if (dcast<const Literal *>(value)) {
    return value;
  } else if (auto tuple = dcast<const Tuple *>(value)) {
    return new Tuple(tuple->location, prefix(bindings, pre, tuple->dims));
  } else if (auto tuple_deref = dcast<const TupleDeref *>(value)) {
    return new TupleDeref(prefix(bindings, pre, tuple_deref->expr),
                          tuple_deref->index, tuple_deref->max);
  } else if (auto sizeof_ = dcast<const Sizeof *>(value)) {
    return new Sizeof(sizeof_->location, prefix(bindings, pre, sizeof_->type));
  } else if (auto break_ = dcast<const Break *>(value)) {
    return break_;
  } else if (auto continue_ = dcast<const Continue *>(value)) {
    return continue_;
  } else if (auto ffi = dcast<const FFI *>(value)) {
    // NOTE: We do not prefix "C" FFI names.
    std::vector<const Expr *> exprs;
    for (auto expr : ffi->exprs) {
      exprs.push_back(prefix(bindings, pre, expr));
    }
    return new FFI(ffi->id, exprs);
  } else if (auto builtin = dcast<const Builtin *>(value)) {
    std::vector<const Expr *> exprs;
    for (auto expr : builtin->exprs) {
      exprs.push_back(prefix(bindings, pre, expr));
    }
    return new Builtin(new Var(builtin->var->id), exprs);
  } else if (auto defer = dcast<const Defer *>(value)) {
    return new Defer(prefix_application(bindings, pre, defer->application));
  } else {
    std::cerr << "What should I do with " << value->str() << "?" << std::endl;
    assert(false);
    return nullptr;
  }
}

std::vector<const Expr *> prefix(const std::set<std::string> &bindings,
                                 std::string pre,
                                 std::vector<const Expr *> values) {
  std::vector<const Expr *> new_values;
  for (auto value : values) {
    new_values.push_back(prefix(bindings, pre, value));
  }
  return new_values;
}

types::Map prefix(const std::set<std::string> &bindings,
                  std::string pre,
                  const types::Map &data_ctors) {
  types::Map new_data_ctors;
  for (auto pair : data_ctors) {
    new_data_ctors[prefix(bindings, pre, pair.first)] = prefix(bindings, pre,
                                                               pair.second);
  }
  return new_data_ctors;
}

ParsedDataCtorsMap prefix(const std::set<std::string> &bindings,
                          std::string pre,
                          const ParsedDataCtorsMap &data_ctors_map) {
  ParsedDataCtorsMap new_data_ctors_map;
  for (auto pair : data_ctors_map) {
    new_data_ctors_map[prefix(bindings, pre, pair.first)] = prefix(
        bindings, pre, pair.second);
  }
  return new_data_ctors_map;
}

const Module *prefix(const std::set<std::string> &bindings,
                     const Module *module) {
  return new Module(module->name, module->imports,
                    prefix(bindings, module->name, module->decls),
                    prefix(bindings, module->name, module->type_decls),
                    prefix(bindings, module->name, module->type_classes),
                    prefix(bindings, module->name, module->instances),
                    prefix(bindings, module->name, module->ctor_id_map,
                           true /*include_keys*/),
                    prefix(bindings, module->name, module->data_ctors_map),
                    prefix(bindings, module->name, module->type_env));
}

types::Scheme::Ref prefix(const std::set<std::string> &bindings,
                          std::string pre,
                          types::Scheme::Ref scheme) {
  return ::scheme(scheme->vars, {},
                  // prefix(bindings, pre, scheme->predicates, false),
                  prefix(bindings, pre, scheme->type));
}

} // namespace zion
