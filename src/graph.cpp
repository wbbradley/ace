#include "graph.h"

#include "dbg.h"
#include "disk.h"
#include "user_error.h"
#include "tld.h"

namespace zion {
namespace graph {
void dfs(FILE *fp,
         std::string node,
         const tarjan::Graph &graph,
         std::unordered_set<std::string> &visited,
         const std::map<std::string, int> &ranks,
         std::set<int> &ranks_seen) {
  if (visited.count(node)) {
    return;
  }
  visited.insert(node);
  ranks_seen.insert(ranks.at(node));
  fprintf(fp, "\t\t\"%s\";\n", tld::strip_prefix(node).c_str());
  for (auto &vertex : get(graph, node, {})) {
    fprintf(fp, "\t\t\"%s\" -> \"%s\";\n", tld::strip_prefix(node).c_str(),
            tld::strip_prefix(vertex).c_str());
    dfs(fp, vertex, graph, visited, ranks, ranks_seen);
  }
}

void rank_sccs(const tarjan::SCCs &sccs,
               std::map<std::string, int> &ranks,
               std::map<int, std::set<std::string>> &rank_nodes) {
  int rank = 1;
  for (auto &scc : sccs) {
    for (auto vertex : scc) {
      ranks[vertex] = rank;
      rank_nodes[rank].insert(vertex);
    }
    rank++;
  }
}

void emit_graphviz_dot(const tarjan::Graph &graph,
                       const tarjan::SCCs &sccs,
                       std::string entry_point,
                       std::string filename) {
  FILE *fp = fopen(filename.c_str(), "wt");
  if (fp == nullptr) {
    throw zion::user_error(INTERNAL_LOC(),
                           "unable to open %s for writing DOT_DEPS",
                           filename.c_str());
  }
  fprintf(fp, "digraph G {\n");
  fprintf(fp, "\tranksep=.75;rankdir=LR;ratio=auto;\n\tsize=\"14,20\";\n");

  std::map<std::string, int> ranks;
  std::map<int, std::set<std::string>> rank_nodes;
  std::set<int> ranks_seen;

  rank_sccs(sccs, ranks, rank_nodes);
  std::unordered_set<std::string> visited;
  dfs(fp, entry_point, graph, visited, ranks, ranks_seen);

  for (auto rank: ranks_seen) {
    if (rank_nodes[rank].size() > 1) {
      fprintf(fp, "\tsubgraph cluster_%d { rank=same; ", rank);
      for (auto node : rank_nodes[rank]) {
        if (visited.count(node)) {
          fprintf(fp, "\"%s\"; ", tld::strip_prefix(node).c_str());
        }
      }
      fprintf(fp, "}\n");
    }
  }
  fprintf(fp, "}\n");
  fclose(fp);
}

} // namespace graph
} // namespace zion
