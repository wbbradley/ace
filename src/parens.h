#include <iostream>

struct parens_t {
    std::ostream &os;
    const int parent_precedence, child_precedence;
    parens_t(std::ostream &os, int parent_precedence, int child_precedence)
        : os(os), parent_precedence(parent_precedence), child_precedence(child_precedence) {
        if (parent_precedence > child_precedence) {
            os << "(";
        }
    }
    ~parens_t() {
        if (parent_precedence > child_precedence) {
            os << ")";
        }
    }
};
