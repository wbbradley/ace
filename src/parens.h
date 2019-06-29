#include <iostream>

struct Parens {
  std::ostream &os;
  const int parent_precedence, child_precedence;
  Parens(std::ostream &os, int parent_precedence, int child_precedence)
      : os(os), parent_precedence(parent_precedence),
        child_precedence(child_precedence) {
    if (parent_precedence > child_precedence) {
      os << "(";
    }
  }
  ~Parens() {
    if (parent_precedence > child_precedence) {
      os << ")";
    }
  }
};
