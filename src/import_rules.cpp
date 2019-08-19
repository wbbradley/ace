#include "import_rules.h"

#include "ast.h"
#include "user_error.h"

namespace zion {

RewriteImportRules solve_rewriting_imports(
    const parser::SymbolImports &symbol_imports,
    const parser::SymbolExports &symbol_exports) {
  std::map<Identifier, Identifier> graph;
  std::set<std::string> legal_exports;
  for (auto &module_pair : symbol_exports) {
    for (auto &id_pair : module_pair.second) {
      debug_above(3, log("%s: %s -> %s", module_pair.first.c_str(),
                         id_pair.first.str().c_str(),
                         id_pair.second.str().c_str()));
      if (id_pair.second.name != id_pair.first.name) {
        /* this export actually leads back to something else */
        if (graph.count(id_pair.first) == 1) {
          throw user_error(id_pair.first.location, "ambiguous export %s vs. %s",
                           id_pair.first.str().c_str(),
                           graph.at(id_pair.first).str().c_str());
        }
        graph[id_pair.first] = id_pair.second;
      } else {
        debug_above(2, log("%s looks authentic in the context of module %s",
                           id_pair.first.str().c_str(),
                           module_pair.first.c_str()));
        legal_exports.insert(id_pair.first.name);
      }
    }
  }

  std::map<Identifier, Identifier> rewriting;

  /* resolve exports */
  for (auto pair : graph) {
    /* |symbol_id| represents the current symbol that needs resolving */
    const auto &symbol_id = pair.first;
    Identifier resolved_id = pair.second;
    std::set<Identifier> visited;
    std::list<Identifier> visited_list;
    while (graph.count(resolved_id) == 1) {
      visited.insert(resolved_id);
      visited_list.push_back(resolved_id);

      /* advance to the next id */
      resolved_id = graph.at(resolved_id);

      /* check if we have looped */
      if (in(resolved_id, visited)) {
        auto error = user_error(resolved_id.location, "circular exports");
        for (auto id : visited_list) {
          error.add_info(id.location, "see: %s", id.str().c_str());
        }
        throw error;
      }
    }
    /* rewrite the graph as we go to avoid wasting time for future traversals */
    for (auto &id : visited_list) {
      graph[id] = resolved_id;
    }
    rewriting.insert({symbol_id, resolved_id});
  }

  std::vector<std::pair<Identifier, Identifier>> illegal_imports;
  for (auto &pair : rewriting) {
    debug_above(1, log("rewriting %s -> %s", pair.first.str().c_str(),
                       pair.second.str().c_str()));
    if (legal_exports.count(pair.second.name) == 0) {
      illegal_imports.push_back(pair);
    }
  }

  /* check for illegal imports */
  for (auto &source_pair : symbol_imports) {
    const std::string &source_module = source_pair.first;
    for (auto &dest_pair : source_pair.second) {
      const std::string &dest_module = dest_pair.first;
      const std::set<Identifier> &symbols = dest_pair.second;
      for (auto &symbol : symbols) {
        debug_above(2,
                    log("checking {%s: {..., %s: %s, ...} for illegal import",
                        source_module.c_str(), dest_module.c_str(),
                        symbol.str().c_str()));
        if (legal_exports.count(dest_module + "." + symbol.name) == 0) {
          illegal_imports.push_back(
              {Identifier{source_module + "." + symbol.name, symbol.location},
               Identifier{dest_module + "." + symbol.name, symbol.location}});
        }
      }
    }
  }

  if (illegal_imports.size() != 0) {
    auto error = user_error(illegal_imports[0].first.location,
                            "%s is not exported or does not exist",
                            illegal_imports[0].second.str().c_str());
    for (int i = 1; i < illegal_imports.size(); ++i) {
      error.add_info(illegal_imports[i].first.location,
                     "error: %s is not exported or does not exist",
                     illegal_imports[i].first.str().c_str(),
                     illegal_imports[i].second.str().c_str());
    }
    throw error;
  }

  return rewriting;
}

Identifier rewrite_identifier(const RewriteImportRules &rewrite_import_rules,
                              const Identifier &id) {
  auto iter = rewrite_import_rules.find(id);
  if (iter != rewrite_import_rules.end()) {
    return iter->second;
  } else {
    return id;
  }
}

const types::Refs rewrite_types(const RewriteImportRules &rewrite_import_rules,
                                types::Refs types) {
  types::Refs new_types;
  for (auto type : types) {
    new_types.push_back(type->rewrite_ids(rewrite_import_rules));
  }
  return new_types;
}

namespace {

using namespace ::zion::ast;

std::vector<const Expr *> rewrite_exprs(
    const RewriteImportRules &rewrite_import_rules,
    const std::vector<const Expr *> &exprs);

const Predicate *rewrite_predicate(
    const RewriteImportRules &rewrite_import_rules,
    const Predicate *predicate) {
  return predicate->rewrite(rewrite_import_rules);
}

const Expr *rewrite_expr(const RewriteImportRules &rewrite_import_rules,
                         const Expr *expr);

const PatternBlock *rewrite_pattern_block(
    const RewriteImportRules &rewrite_import_rules,
    const PatternBlock *pattern_block) {
  return new PatternBlock(
      rewrite_predicate(rewrite_import_rules, pattern_block->predicate),
      rewrite_expr(rewrite_import_rules, pattern_block->result));
}

PatternBlocks rewrite_pattern_blocks(
    const RewriteImportRules &rewrite_import_rules,
    const PatternBlocks &pattern_blocks) {
  PatternBlocks new_pattern_blocks;
  new_pattern_blocks.reserve(pattern_blocks.size());
  for (auto &pattern_block : pattern_blocks) {
    new_pattern_blocks.push_back(
        rewrite_pattern_block(rewrite_import_rules, pattern_block));
  }
  return new_pattern_blocks;
}

const Application *rewrite_application(
    const RewriteImportRules &rewrite_import_rules,
    const Application *application) {
  return new Application(
      rewrite_expr(rewrite_import_rules, application->a),
      rewrite_exprs(rewrite_import_rules, application->params));
}

const Expr *rewrite_expr(const RewriteImportRules &rewrite_import_rules,
                         const Expr *expr) {
  if (dcast<const Literal *>(expr)) {
    return expr;
  } else if (auto static_print = dcast<const StaticPrint *>(expr)) {
    return new StaticPrint(
        static_print->location,
        rewrite_expr(rewrite_import_rules, static_print->expr));
  } else if (auto var = dcast<const Var *>(expr)) {
    return new Var(rewrite_identifier(rewrite_import_rules, var->id));
  } else if (auto lambda = dcast<const Lambda *>(expr)) {
    return new Lambda(
        lambda->vars, rewrite_types(rewrite_import_rules, lambda->param_types),
        (lambda->return_type != nullptr)
            ? lambda->return_type->rewrite_ids(rewrite_import_rules)
            : nullptr,
        rewrite_expr(rewrite_import_rules, lambda->body));
  } else if (auto application = dcast<const Application *>(expr)) {
    return rewrite_application(rewrite_import_rules, application);
  } else if (auto let = dcast<const Let *>(expr)) {
    return new Let(let->var, rewrite_expr(rewrite_import_rules, let->value),
                   rewrite_expr(rewrite_import_rules, let->body));
  } else if (auto condition = dcast<const Conditional *>(expr)) {
    return new Conditional(
        rewrite_expr(rewrite_import_rules, condition->cond),
        rewrite_expr(rewrite_import_rules, condition->truthy),
        rewrite_expr(rewrite_import_rules, condition->falsey));
  } else if (dcast<const Break *>(expr)) {
    return expr;
  } else if (dcast<const Continue *>(expr)) {
    return expr;
  } else if (auto while_ = dcast<const While *>(expr)) {
    return new While(rewrite_expr(rewrite_import_rules, while_->condition),
                     rewrite_expr(rewrite_import_rules, while_->block));
  } else if (auto block = dcast<const Block *>(expr)) {
    std::vector<const Expr *> statements;
    for (auto stmt : block->statements) {
      statements.push_back(rewrite_expr(rewrite_import_rules, stmt));
    }
    return new Block(statements);
  } else if (auto return_ = dcast<const ReturnStatement *>(expr)) {
    return new ReturnStatement(
        rewrite_expr(rewrite_import_rules, return_->value));
  } else if (auto tuple = dcast<const Tuple *>(expr)) {
    std::vector<const Expr *> exprs;
    exprs.reserve(tuple->dims.size());
    for (auto dim : tuple->dims) {
      exprs.push_back(rewrite_expr(rewrite_import_rules, dim));
    }
    return new Tuple(tuple->location, exprs);
  } else if (auto tuple_deref = dcast<const TupleDeref *>(expr)) {
    return new TupleDeref(rewrite_expr(rewrite_import_rules, tuple_deref->expr),
                          tuple_deref->index, tuple_deref->max);
  } else if (auto as = dcast<const As *>(expr)) {
    return new As(rewrite_expr(rewrite_import_rules, as->expr),
                  as->type->rewrite_ids(rewrite_import_rules), as->force_cast);
  } else if (auto sizeof_ = dcast<const Sizeof *>(expr)) {
    return new Sizeof(sizeof_->location,
                      sizeof_->type->rewrite_ids(rewrite_import_rules));
  } else if (auto builtin = dcast<const Builtin *>(expr)) {
    std::vector<const Expr *> exprs;
    exprs.reserve(builtin->exprs.size());
    for (auto expr : builtin->exprs) {
      exprs.push_back(rewrite_expr(rewrite_import_rules, expr));
    }
    return new Builtin(builtin->var, exprs);
  } else if (auto match = dcast<const Match *>(expr)) {
    PatternBlocks pattern_blocks;
    return new Match(
        rewrite_expr(rewrite_import_rules, match->scrutinee),
        rewrite_pattern_blocks(rewrite_import_rules, match->pattern_blocks));
  } else if (auto defer = dcast<const Defer *>(expr)) {
    return new Defer(
        rewrite_application(rewrite_import_rules, defer->application));
  } else {
    assert(false);
    return {};
  }
  assert(false);
  return {};
}

std::vector<const Expr *> rewrite_exprs(
    const RewriteImportRules &rewrite_import_rules,
    const std::vector<const Expr *> &exprs) {
  std::vector<const Expr *> new_exprs;
  new_exprs.reserve(exprs.size());
  for (auto expr : exprs) {
    new_exprs.push_back(rewrite_expr(rewrite_import_rules, expr));
  }
  return new_exprs;
}

std::vector<const Decl *> rewrite_decls(
    const RewriteImportRules &rewrite_import_rules,
    const std::vector<const Decl *> &decls) {
  std::vector<const Decl *> new_decls;
  new_decls.reserve(decls.size());

  for (auto decl : decls) {
    new_decls.push_back(
        new Decl(rewrite_identifier(rewrite_import_rules, decl->id),
                 rewrite_expr(rewrite_import_rules, decl->value)));
  }
  return new_decls;
}

#if 0
std::vector<const TypeDecl> rewrite_type_decls(
    const RewriteImportRules &rewrite_import_rules,
    const std::vector<const TypeDecl> &type_decls) {
#ifdef ZION_DEBUG
  for (auto &type_decl : type_decls) {
    assert(!in(type_decl.id, rewrite_import_rules));
    for (auto &param : type_decl.params) {
      assert(!in(param, rewrite_import_rules));
    }
  }
#endif
  return type_decls;
}
#endif

types::ClassPredicate::Ref rewrite_class_predicate(
    const RewriteImportRules &rewrite_import_rules,
    const types::ClassPredicate::Ref &class_predicate) {
  return std::make_shared<types::ClassPredicate>(
      rewrite_identifier(rewrite_import_rules, class_predicate->classname),
      rewrite_types(rewrite_import_rules, class_predicate->params));
}

types::ClassPredicates rewrite_class_predicates(
    const RewriteImportRules &rewrite_import_rules,
    const types::ClassPredicates &class_predicates) {
  types::ClassPredicates new_class_predicates;
  new_class_predicates.reserve(class_predicates.size());
  for (auto &predicate : class_predicates) {
    new_class_predicates.insert(
        rewrite_class_predicate(rewrite_import_rules, predicate));
  }
  return new_class_predicates;
}

const types::Map rewrite_type_map(
    const RewriteImportRules &rewrite_import_rules,
    const types::Map &type_map) {
  types::Map new_type_map;
  for (auto &pair : type_map) {
    new_type_map.insert(
        {pair.first, pair.second->rewrite_ids(rewrite_import_rules)});
  }
  return new_type_map;
}

const TypeClass *rewrite_type_class(
    const RewriteImportRules &rewrite_import_rules,
    const TypeClass *type_class) {
  return new TypeClass(
      type_class->id, type_class->type_var_ids,
      rewrite_class_predicates(rewrite_import_rules,
                               type_class->class_predicates),
      rewrite_type_map(rewrite_import_rules, type_class->overloads));
}

std::vector<const TypeClass *> rewrite_type_classes(
    const RewriteImportRules &rewrite_import_rules,
    const std::vector<const TypeClass *> &type_classes) {
  std::vector<const TypeClass *> new_type_classes;
  new_type_classes.reserve(type_classes.size());
  for (auto &type_class : type_classes) {
    new_type_classes.push_back(
        rewrite_type_class(rewrite_import_rules, type_class));
  }
  return new_type_classes;
}

const Instance *rewrite_instance(const RewriteImportRules &rewrite_import_rules,
                                 const Instance *instance) {
  return new Instance(
      rewrite_class_predicate(rewrite_import_rules, instance->class_predicate),
      rewrite_decls(rewrite_import_rules, instance->decls));
}

std::vector<const Instance *> rewrite_instances(
    const RewriteImportRules &rewrite_import_rules,
    const std::vector<const Instance *> &instances) {
  std::vector<const Instance *> new_instances;
  new_instances.reserve(instances.size());
  for (auto instance : instances) {
    new_instances.push_back(rewrite_instance(rewrite_import_rules, instance));
  }
  return new_instances;
}

ParsedDataCtorsMap rewrite_data_ctors_map(
    const RewriteImportRules &rewrite_import_rules,
    const ParsedDataCtorsMap &data_ctors_map) {
  ParsedDataCtorsMap new_data_ctors_map;
  for (auto &pair : data_ctors_map) {
    new_data_ctors_map.insert(
        {pair.first, rewrite_type_map(rewrite_import_rules, pair.second)});
  }
  return new_data_ctors_map;
}

const Module *rewrite_module(const RewriteImportRules &rewrite_import_rules,
                             const Module *module) {
  return new Module(
      module->name, module->imports,
      rewrite_decls(rewrite_import_rules, module->decls), module->type_decls,
      rewrite_type_classes(rewrite_import_rules, module->type_classes),
      rewrite_instances(rewrite_import_rules, module->instances),
      module->ctor_id_map,
      rewrite_data_ctors_map(rewrite_import_rules, module->data_ctors_map),
      rewrite_type_map(rewrite_import_rules, module->type_env));
}

} // namespace

std::vector<const ast::Predicate *> rewrite_predicates(
    const RewriteImportRules &rewrite_import_rules,
    const std::vector<const ast::Predicate *> &predicates) {
  std::vector<const ast::Predicate *> new_predicates;
  new_predicates.reserve(predicates.size());

  for (auto &predicate : predicates) {
    new_predicates.push_back(
        rewrite_predicate(rewrite_import_rules, predicate));
  }
  return new_predicates;
}

std::vector<const Module *> rewrite_modules(
    const RewriteImportRules &rewrite_import_rules,
    const std::vector<const Module *> &modules) {
  std::vector<const Module *> new_modules;
  new_modules.reserve(modules.size());
  for (auto module : modules) {
    new_modules.push_back(rewrite_module(rewrite_import_rules, module));
  }
  return new_modules;
}

} // namespace zion
