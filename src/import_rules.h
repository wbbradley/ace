#pragma once

#include <map>
#include <string>

#include "identifier.h"
#include "parse_state.h"

namespace zion {

typedef std::map<std::string, std::map<Identifier, Identifier>>
    RewriteImportRules;

RewriteImportRules solve_rewriting_imports(
    const parser::SymbolImports &symbol_imports,
    const parser::SymbolExports &symbol_exports);

} // namespace zion
