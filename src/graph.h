#pragma once

#include <string>

#include "tarjan.h"

namespace zion {
namespace graph {
void emit_graphviz_dot(const tarjan::Graph &graph,
                       const tarjan::SCCs &sccs,
                       std::string entry_point,
                       std::string filename);
}
} // namespace zion
