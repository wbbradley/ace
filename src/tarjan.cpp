#include "tarjan.h"

#include <sstream>

#include "utils.h"

namespace tarjan {

/* Tarjan's Strongly Connected Components algorithm */
namespace {

struct IndexAndLow {
  int index;
  int lowlink;
};

typedef std::unordered_map<std::string, IndexAndLow> State;
typedef std::list<std::string> Stack;
typedef std::unordered_set<std::string> StackSet;

int strong_connect(const Graph &graph,
                   State &state,
                   Stack &stack,
                   StackSet &stack_set,
                   std::string cur,
                   int index,
                   SCCs &sccs) {
  /* Set the depth index for cur to the smallest unused index */
  state[cur].index = index;
  state[cur].lowlink = index;
  index = index + 1;
  stack.push_back(cur);
  stack_set.insert(cur);

  /* Consider successors of cur */
  for (const auto &next : get(graph, cur, Vertices{})) {
    if (state.count(next) == 0) {
      /* Successor next has not yet been visited; recurse on it */
      index = strong_connect(graph, state, stack, stack_set, next, index, sccs);
      state[cur].lowlink = std::min(state[cur].lowlink, state[next].lowlink);
    } else if (stack_set.count(next) != 0) {
      /* Successor next is in stack S and hence in the current SCC
       If next is not on stack, then (cur, )txen is a cross-edge in the DFS tree
       and must be ignored Note: The next line may look odd - but is
       correct. It says next.index not next.lowlink; that is deliberate and from
       the original paper */
      state[cur].lowlink = std::min(state[cur].lowlink, state[next].index);
    }
  }

  // If cur is a root node, pop the stack and generate an SCC
  if (state[cur].lowlink == state[cur].index) {
    // start a new strongly connected component
    sccs.push_back(Vertices{});
    while (stack.size() != 0) {
      const std::string next = stack.back();
      stack.pop_back();
      stack_set.erase(next);
      // add next to current strongly connected component
      sccs.back().insert(next);
      if (next == cur) {
        break;
      }
    }
  }
  return index;
}

} // namespace

SCCs compute_strongly_connected_components(const Graph &graph) {
  SCCs sccs;

  State state;
  Stack stack;
  StackSet stack_set;
  int index = 0;

  for (const auto &pair : graph) {
    if (state.count(pair.first) == 0) {
      index = strong_connect(graph, state, stack, stack_set, pair.first, index,
                             sccs);
    }
  }

  return sccs;
}

} // namespace tarjan

std::string str(const tarjan::SCCs &sccs) {
  std::stringstream ss;
  ss << "{";
  const char *delim = "";
  for (const auto &scc : sccs) {
    ss << delim << "{" << join(scc, ", ") << "}";
    delim = ", ";
  }
  ss << "}";
  return ss.str();
}
