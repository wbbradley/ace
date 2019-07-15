#pragma once

#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tarjan {

/* Tarjan's Strongly Connected Components algorithm */
typedef std::unordered_set<std::string> Vertices;
typedef std::unordered_map<std::string, Vertices> Graph;

typedef std::list<std::unordered_set<std::string>> SCCs;

SCCs compute_strongly_connected_components(const Graph &graph);

} // namespace tarjan

std::string str(const tarjan::SCCs &sccs);
