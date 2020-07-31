#pragma once

#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <set>

namespace tarjan {

/* Tarjan's Strongly Connected Components algorithm */
typedef std::set<std::string> Vertices;
typedef std::unordered_map<std::string, Vertices> Graph;

typedef std::list<Vertices> SCCs;

SCCs compute_strongly_connected_components(const Graph &graph);

} // namespace tarjan

std::string str(const tarjan::SCCs &sccs);
