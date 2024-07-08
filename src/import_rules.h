#pragma once

#include <map>
#include <string>
#include <vector>

#include "ast_decls.h"
#include "identifier.h"
#include "parse_state.h"

namespace cider {

typedef std::map<Identifier, Identifier> RewriteImportRules;

RewriteImportRules solve_rewriting_imports(
    const parser::SymbolImports &symbol_imports,
    const parser::SymbolExports &symbol_exports);

std::vector<const ast::Module *> rewrite_modules(
    const RewriteImportRules &rewrite_import_rules,
    const std::vector<const ast::Module *> &modules);

std::vector<const ast::Predicate *> rewrite_predicates(
    const RewriteImportRules &rewrite_import_rules,
    const std::vector<const ast::Predicate *> &predicates);

Identifier rewrite_identifier(const RewriteImportRules &rewrite_import_rules,
                              const Identifier &id);

const types::Refs rewrite_types(const RewriteImportRules &rewrite_import_rules,
                                types::Refs types);
} // namespace cider
