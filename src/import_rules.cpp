#include "import_rules.h"

#include "user_error.h"

namespace zion {

RewriteImportRules solve_rewriting_imports(
    const parser::SymbolImports &symbol_imports,
    const parser::SymbolExports &symbol_exports) {
  std::map<Identifier, Identifier> graph;

  for (auto &module_pair : symbol_exports) {
    for (auto &id_pair : module_pair.second) {
      debug_above(3, log("%s: %s -> %s", module_name.c_str(),
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

  for (auto &pair : rewriting) {
    log("rewriting %s -> %s", pair.first.str().c_str(),
        pair.second.str().c_str());
  }

  return {};
}

} // namespace zion
